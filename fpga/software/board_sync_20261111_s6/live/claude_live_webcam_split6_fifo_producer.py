# Claude 2026-11-11: copy of Codex fsoc_no_live_webcam_helix_split72x_fifo_producer.py changed to emit raw transport blocks (TX split 6).
#!/usr/bin/env python3
"""Capture webcam H.264 and generate HELIX Split-7.2x packets continuously.

Target:
  fsoc_no / Logitech C270 / RFSoC-PYNQ 2024.1.

Pipeline:
  /dev/video0 -> FFmpeg H.264 -> streaming HLVX transport blocks
  -> four-core bit-exact 5G TX generation -> POSIX packet FIFO.

The FIFO contains only complete 49,056-byte original-HELIX Split-7.2x units.
Backpressure from the RF DMA reader propagates safely to FFmpeg.

Execution:
  PYTHONPATH=/home/xilinx/helix_py3gpp \
    /usr/local/share/pynq-venv/bin/python3 -u \
    fsoc_no_live_webcam_helix_split72x_fifo_producer.py \
    --fifo /home/xilinx/helix_webcam_capture/live/tx_split72x.fifo \
    --session-dir /home/xilinx/helix_webcam_capture/live \
    --duration 15

Expected output files:
  fsoc_no_live_source.h264
  fsoc_no_live_transport_blocks.bin
  fsoc_no_live_manifest.json
  fsoc_no_live_ffmpeg.log

Next step:
  Run the v19 FIFO RF consumer concurrently and validate its Split-6 output
  with fsoc_no_decode_reassemble_split6_h264_onboard.py.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import multiprocessing as mp
import os
import stat
import struct
import subprocess
import time
import zlib
from pathlib import Path

os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")

import numpy as np

import fsoc_no_py3gpp_nre73_encoder_bitexact_benchmark as helix5g


SCRIPT_DIR = Path("/home/xilinx/helix_webcam_capture/py3gpp_bench_stage")  # Claude: Codex stage dir (indices + encoder module)
TEMPLATE_PATH = Path(
    "/home/xilinx/jupyter_notebooks/"
    "helix_rfsoc4x2_nre73_ofdm_fifo_threshold64_full_slot_"
    "dac_ingress_v15/slotFR2_CH1_7.2x_TX1.txt"
)
PDSCH_INDICES_PATH = (
    SCRIPT_DIR / "fsoc_no_helix_nre73_pdsch_indices_u32le.bin"
)
TRANSPORT_BLOCK_BYTES = 1345
TRANSPORT_BLOCK_BITS = TRANSPORT_BLOCK_BYTES * 8
HEADER_BYTES = 32
PAYLOAD_BYTES = TRANSPORT_BLOCK_BYTES - HEADER_BYTES
# Reserve enough room for two independent GF(256) parity packets per group.
# This corrects up to two complete logical-packet erasures after all PHY
# variants have been tried; it does not hide CRC failures inside a packet.
DATA_PAYLOAD_BYTES = 1280
FEC_GROUP_SIZE = 8
FEC_PARITY_COUNT = 2
FEC_FLAG = 1 << 2
FEC_MAGIC = b"FEC2"
FEC_HEADER = struct.Struct("<4sBBBBH8HB")
OUTPUT_BITS = 21054
SPLIT72X_WORDS = 676  # Claude: this copy emits raw transport blocks padded to 1352 bytes (TX split 6), name kept to limit the diff
STREAM_ID = 0x46534F43
LIVE_PACKET_COUNT_FIELD = 0xFFFF

STATE: dict[str, object] = {}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fifo", type=Path, required=True)
    parser.add_argument("--session-dir", type=Path, required=True)
    parser.add_argument("--duration", type=float, default=15.0)
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--frame-id", type=int, default=1)
    parser.add_argument("--bitrate-kbps", type=int, default=600)
    parser.add_argument("--framerate", type=int, default=10)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--variant-count", type=int, default=1)
    return parser.parse_args()


def make_transport_block(
    payload: bytes,
    packet_index: int,
    frame_id: int,
    first_packet: bool,
    final_packet: bool,
    extra_flags: int = 0,
) -> bytes:
    flags = int(first_packet) | (int(final_packet) << 1) | int(extra_flags)
    first_28 = struct.pack(
        "<4sBBHIIHHHHI",
        b"HLVX",
        1,
        flags,
        HEADER_BYTES,
        STREAM_ID,
        int(frame_id) & 0xFFFFFFFF,
        packet_index,
        LIVE_PACKET_COUNT_FIELD,
        len(payload),
        0,
        zlib.crc32(payload) & 0xFFFFFFFF,
    )
    header = first_28 + struct.pack(
        "<I",
        zlib.crc32(first_28) & 0xFFFFFFFF,
    )
    return (header + payload).ljust(TRANSPORT_BLOCK_BYTES, b"\x00")


def gf_mul_bytes(data: bytes, coefficient: int) -> bytes:
    """Multiply a byte vector by one GF(256) scalar (x^8+x^4+x^3+x^2+1)."""
    source = np.frombuffer(data, dtype=np.uint8)
    result = np.zeros(source.shape, dtype=np.uint8)
    term = source.copy()
    value = int(coefficient) & 0xFF
    while value:
        if value & 1:
            result ^= term
        high = (term & np.uint8(0x80)) != 0
        term = (term << np.uint8(1)).astype(np.uint8, copy=False)
        term[high] ^= np.uint8(0x1D)
        value >>= 1
    return result.tobytes()


def make_fec_payloads(group: list[tuple[int, bytes, bool, bool]]) -> list[bytes]:
    if not group or len(group) > FEC_GROUP_SIZE:
        raise ValueError("invalid FEC group size")
    lengths = [len(item[1]) for item in group]
    if any(length > DATA_PAYLOAD_BYTES for length in lengths):
        raise ValueError("FEC data payload exceeds fixed symbol size")
    lengths.extend([0] * (FEC_GROUP_SIZE - len(lengths)))
    final_positions = [index for index, item in enumerate(group) if item[3]]
    final_position = final_positions[0] if final_positions else 0xFF
    padded = [item[1].ljust(DATA_PAYLOAD_BYTES, b"\x00") for item in group]
    parity0 = np.zeros(DATA_PAYLOAD_BYTES, dtype=np.uint8)
    parity1 = np.zeros(DATA_PAYLOAD_BYTES, dtype=np.uint8)
    for position, data in enumerate(padded):
        vector = np.frombuffer(data, dtype=np.uint8)
        parity0 ^= vector
        parity1 ^= np.frombuffer(
            gf_mul_bytes(data, position + 1), dtype=np.uint8
        )
    payloads = []
    for parity_index, parity in enumerate((parity0, parity1)):
        metadata = FEC_HEADER.pack(
            FEC_MAGIC,
            1,
            parity_index,
            len(group),
            0,
            DATA_PAYLOAD_BYTES,
            *lengths,
            final_position,
        )
        payload = metadata + parity.tobytes()
        if len(payload) > PAYLOAD_BYTES:
            raise RuntimeError("FEC parity payload exceeds HLVX capacity")
        payloads.append(payload)
    return payloads


def generate_split72x(item: tuple[int, bytes]) -> tuple[int, bytes]:
    packet_index, transport_bytes = item
    return packet_index, bytes(transport_bytes).ljust(1352, b"\x00")  # Claude: TX split 6 -- the FPGA does CRC/LDPC/rate matching


def main() -> int:
    args = parse_args()
    fifo_path = args.fifo.resolve()
    session_dir = args.session_dir.resolve()
    if args.duration <= 0 or args.duration > 3600:
        raise ValueError("--duration must be in (0, 3600]")
    if not 1 <= args.workers <= 4:
        raise ValueError("--workers must be in 1..4")
    if not 100 <= args.bitrate_kbps <= 1400:
        raise ValueError("--bitrate-kbps must be in 100..1400")
    if not 1 <= args.framerate <= 30:
        raise ValueError("--framerate must be in 1..30")
    if (args.width, args.height) not in ((1280, 720), (1920, 1080)):
        raise ValueError("supported resolutions are 1280x720 and 1920x1080")
    if not 1 <= args.variant_count <= 16:
        raise ValueError("--variant-count must be in 1..16")
    if not fifo_path.exists() or not stat.S_ISFIFO(
        fifo_path.stat().st_mode
    ):
        raise FileNotFoundError(f"Not a POSIX FIFO: {fifo_path}")
    for required in (PDSCH_INDICES_PATH,):
        if not required.is_file():
            raise FileNotFoundError(required)

    session_dir.mkdir(parents=True, exist_ok=True)
    source_path = session_dir / "fsoc_no_live_source.h264"
    transport_path = session_dir / "fsoc_no_live_transport_blocks.bin"
    manifest_path = session_dir / "fsoc_no_live_manifest.json"
    ffmpeg_log_path = session_dir / "fsoc_no_live_ffmpeg.log"
    for output in (
        source_path,
        transport_path,
        manifest_path,
        ffmpeg_log_path,
    ):
        if output.exists():
            output.unlink()

    template_words = None  # Claude: not needed for Split-7.3
    pdsch_indices = np.fromfile(
        PDSCH_INDICES_PATH,
        dtype="<u4",
    ).astype(np.int64)
    bgn = int(
        helix5g.nrDLSCHInfo(TRANSPORT_BLOCK_BITS, 0.4785)["BGN"]
    )
    placeholder = np.zeros(TRANSPORT_BLOCK_BYTES, dtype=np.uint8)
    init_started = time.perf_counter()
    first_code_blocks = helix5g.transport_block_to_code_blocks(
        placeholder,
        bgn,
    )
    ldpc_encoder = helix5g.CachedLDPCEncoder(first_code_blocks, bgn)
    pdsch_modulator = helix5g.CachedPDSCHQPSK(OUTPUT_BITS)
    init_seconds = time.perf_counter() - init_started
    STATE.update(
        bgn=bgn,
        ldpc_encoder=ldpc_encoder,
        template_words=template_words,
        pdsch_indices=pdsch_indices,
        pdsch_modulator=pdsch_modulator,
    )

    bitrate = f"{args.bitrate_kbps}k"
    ffmpeg_command = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "info",
        "-f",
        "v4l2",
        "-input_format",
        "mjpeg",
        "-framerate",
        str(args.framerate),
        "-video_size",
        f"{args.width}x{args.height}",
        "-i",
        "/dev/video0",
        "-t",
        str(args.duration),
        "-an",
        "-c:v",
        "libx264",
        "-preset",
        "ultrafast",
        "-tune",
        "zerolatency",
        "-profile:v",
        "baseline",
        "-level:v",
        "4.0" if args.height > 720 else "3.1",
        "-pix_fmt",
        "yuv420p",
        "-b:v",
        bitrate,
        "-maxrate",
        bitrate,
        "-bufsize",
        bitrate,
        "-g",
        str(args.framerate),
        "-keyint_min",
        str(args.framerate),
        "-sc_threshold",
        "0",
        "-vsync",
        "0",
        "-f",
        "h264",
        "pipe:1",
    ]

    print("BOARD=fsoc_no", flush=True)
    print(f"LIVE_CACHE_INIT_SECONDS={init_seconds:.6f}", flush=True)
    print(f"LIVE_FIFO_OPEN_WAIT path={fifo_path}", flush=True)

    packet_count = 0
    encoded_variant_count = 0
    source_bytes = 0
    split72x_digest = hashlib.sha256()
    stream_started = None
    context = mp.get_context("fork")

    with (
        fifo_path.open("wb", buffering=0) as fifo_output,
        source_path.open("wb") as source_output,
        transport_path.open("wb") as transport_output,
        ffmpeg_log_path.open("wb") as ffmpeg_log,
        context.Pool(processes=args.workers) as pool,
    ):
        # Exclude the intentional wait for the RF FIFO reader from live
        # throughput. Timing starts only after both FIFO endpoints connect.
        stream_started = time.perf_counter()
        process = subprocess.Popen(
            ffmpeg_command,
            stdout=subprocess.PIPE,
            stderr=ffmpeg_log,
            bufsize=0,
        )
        if process.stdout is None:
            raise RuntimeError("FFmpeg stdout pipe was not created")

        def transport_items():
            nonlocal source_bytes
            packet_index = 0
            current = process.stdout.read(DATA_PAYLOAD_BYTES)
            group: list[tuple[int, bytes, bool, bool]] = []
            while current:
                following = process.stdout.read(DATA_PAYLOAD_BYTES)
                source_output.write(current)
                source_bytes += len(current)
                group.append(
                    (packet_index, current, packet_index == 0, not following)
                )
                if len(group) == FEC_GROUP_SIZE or not following:
                    for logical_index, payload, first_packet, final_packet in group:
                        for variant_index in range(args.variant_count):
                            # Changing frame_id changes the legal HLVX header CRC,
                            # transport CRC, LDPC codeword and QPSK waveform while
                            # preserving payload and logical packet index.
                            transport = make_transport_block(
                                payload,
                                logical_index,
                                args.frame_id + variant_index,
                                first_packet=first_packet,
                                final_packet=final_packet,
                            )
                            transport_output.write(transport)
                            yield logical_index, transport
                    group_start = group[0][0]
                    for parity_index, fec_payload in enumerate(
                        make_fec_payloads(group)
                    ):
                        for variant_index in range(args.variant_count):
                            transport = make_transport_block(
                                fec_payload,
                                group_start,
                                args.frame_id
                                + variant_index
                                + (parity_index + 1) * args.variant_count,
                                first_packet=False,
                                final_packet=False,
                                extra_flags=FEC_FLAG,
                            )
                            transport_output.write(transport)
                            yield group_start, transport
                    group.clear()
                packet_index += 1
                current = following

        for packet_index, split72x_bytes in pool.imap(
            generate_split72x,
            transport_items(),
            chunksize=1,
        ):
            if len(split72x_bytes) != SPLIT72X_WORDS * 2:
                raise RuntimeError("Generated live Split-7.2x size mismatch")
            fifo_output.write(split72x_bytes)
            split72x_digest.update(split72x_bytes)
            encoded_variant_count += 1
            packet_count = max(packet_count, packet_index + 1)
            if (
                encoded_variant_count == 1
                or encoded_variant_count % 100 == 0
            ):
                print(
                    "LIVE_PRODUCED_PACKET "
                    f"logical_index={packet_index} "
                    f"encoded_variant={encoded_variant_count} "
                    f"bytes={len(split72x_bytes)}",
                    flush=True,
                )

        process.stdout.close()
        ffmpeg_return_code = process.wait(timeout=10)
        if ffmpeg_return_code:
            raise RuntimeError(
                f"FFmpeg returned {ffmpeg_return_code}; see {ffmpeg_log_path}"
            )

    if stream_started is None:
        raise RuntimeError("Live stream timing was not initialized")
    elapsed = time.perf_counter() - stream_started
    if packet_count < 1 or source_bytes < 1:
        raise RuntimeError("Live webcam producer generated no H.264 packets")

    source_sha256 = hashlib.sha256(source_path.read_bytes()).hexdigest()
    manifest = {
        "schema": "helix-video-live-nre73-v2-fec2",
        "board": "fsoc_no",
        "generator": "fsoc_no concurrent webcam and bit-exact 5G encoder",
        "phy": (
            "original HELIX N_RE73 QPSK rate 490/1024 "
            "Split7.2x-to-Split6"
        ),
        "source_h264": str(source_path),
        "source_bytes": source_bytes,
        "source_sha256": source_sha256,
        "packet_count": packet_count,
        "variant_count": args.variant_count,
        "encoded_variant_count": encoded_variant_count,
        "hlvx_packet_count_field": LIVE_PACKET_COUNT_FIELD,
        "transport_block_bits": TRANSPORT_BLOCK_BITS,
        "transport_block_bytes": TRANSPORT_BLOCK_BYTES,
        "header_bytes": HEADER_BYTES,
        "payload_bytes_per_packet": PAYLOAD_BYTES,
        "data_payload_bytes_per_packet": DATA_PAYLOAD_BYTES,
        "fec_group_size": FEC_GROUP_SIZE,
        "fec_parity_packets_per_group": FEC_PARITY_COUNT,
        "transport_blocks_file": str(transport_path),
        "split72x_fifo": str(fifo_path),
        "split72x_words_per_packet": SPLIT72X_WORDS,
        "split72x_bytes_per_packet": SPLIT72X_WORDS * 2,
        "split72x_stream_sha256": split72x_digest.hexdigest(),
        "pdsch_indices_file": str(PDSCH_INDICES_PATH),
        "rf_center_mhz": 1840,
        "approved_bandwidth_mhz": 210.24,
        "duration_requested_seconds": args.duration,
        "framerate_requested": args.framerate,
        "width_requested": args.width,
        "height_requested": args.height,
        "bitrate_requested_kbps": args.bitrate_kbps,
        "workers": args.workers,
        "cached_initialization_seconds": init_seconds,
        "stream_elapsed_seconds": elapsed,
        "source_payload_mbps": (
            source_bytes * 8.0 / elapsed / 1_000_000.0
        ),
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(f"LIVE_SOURCE_BYTES={source_bytes}", flush=True)
    print(f"LIVE_PACKET_COUNT={packet_count}", flush=True)
    print(f"LIVE_ENCODED_VARIANT_COUNT={encoded_variant_count}", flush=True)
    print(f"LIVE_STREAM_SECONDS={elapsed:.6f}", flush=True)
    print(
        f"LIVE_SOURCE_PAYLOAD_MBPS={manifest['source_payload_mbps']:.6f}",
        flush=True,
    )
    print(f"LIVE_MANIFEST={manifest_path}", flush=True)
    print("FSOC_NO_LIVE_SPLIT72X_FIFO_PRODUCER_PASS=1", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
