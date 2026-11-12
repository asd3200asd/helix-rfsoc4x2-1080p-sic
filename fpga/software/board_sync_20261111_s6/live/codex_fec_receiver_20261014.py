#!/usr/bin/env python3
"""Decode live HELIX Split 6 and display it through FFmpeg/MJPEG.

Target:
  fsoc_no / RFSoC4x2 / RFSoC-PYNQ 2024.1 / HELIX N_RE73 Split 6.

Pipeline:
  raw 1,408-byte Split-6 FIFO packets
  -> code-block desegment + CRC24B/CRC24A
  -> HLVX header/payload CRC
  -> Annex-B H.264 bytes
  -> FFmpeg low-latency decoder
  -> latest-frame MJPEG HTTP service.

The received H.264 remains 1280x720. The browser preview defaults to
640x360 at 5 fps to avoid competing excessively with the A53 webcam encoder
and four HELIX TX workers.

Execution:
  PYTHONPATH=/home/xilinx/helix_py3gpp \
    /usr/local/share/pynq-venv/bin/python3 -B -u \
    fsoc_no_live_split6_to_ffmpeg_mjpeg_receiver.py \
    --split6-fifo /home/xilinx/helix_webcam_capture/live/rx_split6.fifo \
    --session-dir /home/xilinx/helix_webcam_capture/live \
    --http-port 8090

Expected outputs:
  http://192.168.3.1:8090/
  fsoc_no_live_display_recovered.h264
  fsoc_no_live_display_receiver_summary.json
  fsoc_no_live_display_ffmpeg.log

Expected success markers:
  LIVE_DISPLAY_FIRST_FRAME
  LIVE_SPLIT6_RECEIVER_EOF
  FSOC_NO_LIVE_DISPLAY_RECEIVER_PASS=1

Next step:
  For the future two-board test, deploy this receiver on the RFSoC4x2
  receiver role and point its browser or Jetson display at port 8090;
  no QSFP interface is required.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import signal
import stat
import struct
import subprocess
import threading
import time
import zlib
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import numpy as np

import fsoc_no_decode_reassemble_split6_h264_onboard as split6
import fsoc_no_py3gpp_nre73_encoder_bitexact_benchmark as helix5g


SPLIT6_BYTES_PER_PACKET = 1408
TRANSPORT_BLOCK_BITS = 10760
TARGET_CODE_RATE = 0.4785
EXPECTED_STREAM_ID = 0x46534F43
LIVE_PACKET_COUNT_FIELD = 0xFFFF
DATA_PAYLOAD_BYTES = 1280
FEC_GROUP_SIZE = 8
FEC_FLAG = 1 << 2
FEC_MAGIC = b"FEC2"
FEC_HEADER = struct.Struct("<4sBBBBH8HB")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--split6-fifo", type=Path, required=True)
    parser.add_argument("--session-dir", type=Path, required=True)
    parser.add_argument("--http-host", default="0.0.0.0")
    parser.add_argument("--http-port", type=int, default=8090)
    parser.add_argument("--board-label", default="rfsoc4x2_6")
    parser.add_argument("--preview-width", type=int, default=640)
    parser.add_argument("--preview-fps", type=int, default=5)
    parser.add_argument(
        "--display-mode",
        choices=("mjpeg", "raw-h264"),
        default="mjpeg",
        help=(
            "mjpeg decodes on the RFSoC A53; raw-h264 serves the validated "
            "Annex-B stream unchanged so the PC performs display decoding"
        ),
    )
    parser.add_argument(
        "--stop-file",
        type=Path,
        help=(
            "Optional marker created immediately after the final ordered "
            "HLVX packet is CRC-validated and forwarded to FFmpeg"
        ),
    )
    parser.add_argument(
        "--ack-file",
        type=Path,
        help=(
            "Optional cumulative physical-packet ACK file. It is updated "
            "only after the current Split-6 packet has been fully decoded, "
            "allowing the RF capture role to re-arm without outrunning this "
            "consumer."
        ),
    )
    parser.add_argument(
        "--keep-serving-seconds",
        type=float,
        default=0,
        help="Zero keeps the last frame available until the process is stopped",
    )
    return parser.parse_args()


def read_exact_packet(fifo_stream, packet_bytes: int) -> bytes | None:
    chunks: list[bytes] = []
    remaining = packet_bytes
    while remaining:
        chunk = fifo_stream.read(remaining)
        if not chunk:
            if remaining == packet_bytes:
                return None
            raise RuntimeError(
                "Split-6 FIFO ended mid-packet: "
                f"{packet_bytes - remaining}/{packet_bytes} bytes"
            )
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def gf_mul_scalar(left: int, right: int) -> int:
    result = 0
    a = int(left) & 0xFF
    b = int(right) & 0xFF
    while b:
        if b & 1:
            result ^= a
        a = ((a << 1) ^ (0x11D if a & 0x80 else 0)) & 0xFF
        b >>= 1
    return result


def gf_inverse(value: int) -> int:
    if not value:
        raise ZeroDivisionError("zero has no GF(256) inverse")
    for candidate in range(1, 256):
        if gf_mul_scalar(value, candidate) == 1:
            return candidate
    raise RuntimeError("GF(256) inverse search failed")


def gf_mul_vector(vector: np.ndarray, coefficient: int) -> np.ndarray:
    result = np.zeros(vector.shape, dtype=np.uint8)
    term = vector.astype(np.uint8, copy=True)
    value = int(coefficient) & 0xFF
    while value:
        if value & 1:
            result ^= term
        high = (term & np.uint8(0x80)) != 0
        term = (term << np.uint8(1)).astype(np.uint8, copy=False)
        term[high] ^= np.uint8(0x1D)
        value >>= 1
    return result


def parse_fec_payload(payload: bytes, group_start: int) -> dict[str, object]:
    if len(payload) != FEC_HEADER.size + DATA_PAYLOAD_BYTES:
        raise ValueError("FEC payload length mismatch")
    unpacked = FEC_HEADER.unpack(payload[: FEC_HEADER.size])
    magic, version, parity_index, group_count, reserved, symbol_bytes = unpacked[:6]
    lengths = tuple(int(value) for value in unpacked[6:14])
    final_position = int(unpacked[14])
    if (
        magic != FEC_MAGIC
        or version != 1
        or parity_index not in (0, 1)
        or not 1 <= group_count <= FEC_GROUP_SIZE
        or reserved != 0
        or symbol_bytes != DATA_PAYLOAD_BYTES
        or group_start % FEC_GROUP_SIZE != 0
        or any(length < 0 or length > DATA_PAYLOAD_BYTES for length in lengths)
        or any(lengths[index] == 0 for index in range(group_count))
        or any(lengths[index] != 0 for index in range(group_count, FEC_GROUP_SIZE))
        or final_position not in (*range(group_count), 0xFF)
    ):
        raise ValueError("FEC metadata validation failed")
    return {
        "group_start": group_start,
        "parity_index": parity_index,
        "group_count": group_count,
        "lengths": lengths,
        "final_position": final_position,
        "symbol": np.frombuffer(payload[FEC_HEADER.size :], dtype=np.uint8).copy(),
    }


class DisplayState:
    def __init__(self, board_label: str):
        self.board_label = board_label
        self.condition = threading.Condition()
        self.jpeg: bytes | None = None
        self.frame_number = 0
        self.rx_packets = 0
        self.accepted_packets = 0
        self.duplicate_packets = 0
        self.sequence_errors = 0
        self.code_block_crc_errors = 0
        self.transport_crc_errors = 0
        self.hlvx_errors = 0
        self.fec_parity_packets = 0
        self.fec_recovered_packets = 0
        self.fec_unrecoverable_groups = 0
        self.payload_bytes = 0
        self.first_packet_time: float | None = None
        self.last_packet_time: float | None = None
        self.ended = False
        self.message = "waiting for Split-6 FIFO"
        self.h264_chunks: list[bytes] = []
        self.h264_client_connections = 0
        self.h264_client_ever_connected = False
        self.h264_bytes_served = 0

    def publish_jpeg(self, jpeg: bytes) -> None:
        with self.condition:
            self.jpeg = jpeg
            self.frame_number += 1
            if self.frame_number == 1:
                print(
                    f"LIVE_DISPLAY_FIRST_FRAME bytes={len(jpeg)}",
                    flush=True,
                )
            self.condition.notify_all()

    def publish_h264(self, payload: bytes) -> None:
        with self.condition:
            self.h264_chunks.append(payload)
            self.condition.notify_all()

    def snapshot(self) -> dict[str, object]:
        with self.condition:
            return {
                "board": self.board_label,
                "frame_number": self.frame_number,
                "rx_packets": self.rx_packets,
                "accepted_packets": self.accepted_packets,
                "duplicate_packets": self.duplicate_packets,
                "sequence_errors": self.sequence_errors,
                "code_block_crc_errors": self.code_block_crc_errors,
                "transport_crc_errors": self.transport_crc_errors,
                "hlvx_errors": self.hlvx_errors,
                "fec_parity_packets": self.fec_parity_packets,
                "fec_recovered_packets": self.fec_recovered_packets,
                "fec_unrecoverable_groups": self.fec_unrecoverable_groups,
                "payload_bytes": self.payload_bytes,
                "ended": self.ended,
                "message": self.message,
                "h264_client_connections": self.h264_client_connections,
                "h264_client_ever_connected": self.h264_client_ever_connected,
                "h264_bytes_served": self.h264_bytes_served,
            }


class DisplayHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, server_address, handler, state: DisplayState):
        super().__init__(server_address, handler)
        self.state = state


class DisplayHandler(BaseHTTPRequestHandler):
    server: DisplayHTTPServer

    def log_message(self, format_string: str, *args) -> None:
        return

    def send_no_cache_headers(self) -> None:
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate")
        self.send_header("Pragma", "no-cache")
        self.send_header("Access-Control-Allow-Origin", "*")

    def do_GET(self) -> None:
        if self.path in ("/", "/index.html"):
            html = """<!doctype html>
<html lang="zh-Hant">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>fsoc_no HELIX Live RX</title>
<style>
body{margin:0;background:#101318;color:#eef;font-family:system-ui;text-align:center}
main{max-width:1000px;margin:auto;padding:18px}
img{width:100%;height:auto;background:#000;border:1px solid #39414d}
#s{white-space:pre-wrap;text-align:left;background:#171c23;padding:10px}
</style>
</head>
<body><main>
<h2>fsoc_no HELIX Split 6 即時接收</h2>
<img src="/stream.mjpg" alt="等待接收影像">
<pre id="s">waiting...</pre>
<script>
setInterval(async()=>{try{
 const r=await fetch('/status.json',{cache:'no-store'});
 document.getElementById('s').textContent=JSON.stringify(await r.json(),null,2);
}catch(e){}},500);
</script>
</main></body></html>""".encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(html)))
            self.send_no_cache_headers()
            self.end_headers()
            self.wfile.write(html)
            return

        if self.path == "/status.json":
            body = json.dumps(
                self.server.state.snapshot(),
                sort_keys=True,
            ).encode("utf-8")
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_no_cache_headers()
            self.end_headers()
            self.wfile.write(body)
            return

        if self.path == "/snapshot.jpg":
            with self.server.state.condition:
                jpeg = self.server.state.jpeg
            if jpeg is None:
                self.send_error(HTTPStatus.SERVICE_UNAVAILABLE)
                return
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Content-Length", str(len(jpeg)))
            self.send_no_cache_headers()
            self.end_headers()
            self.wfile.write(jpeg)
            return

        if self.path == "/live.h264":
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "video/h264")
            self.send_no_cache_headers()
            self.end_headers()
            chunk_index = 0
            with self.server.state.condition:
                self.server.state.h264_client_connections += 1
                self.server.state.h264_client_ever_connected = True
                self.server.state.condition.notify_all()
            print("LIVE_RAW_H264_CLIENT_CONNECTED", flush=True)
            try:
                while True:
                    with self.server.state.condition:
                        self.server.state.condition.wait_for(
                            lambda: (
                                chunk_index < len(self.server.state.h264_chunks)
                                or self.server.state.ended
                            ),
                            timeout=1.0,
                        )
                        chunks = self.server.state.h264_chunks[chunk_index:]
                        ended = self.server.state.ended
                    for chunk in chunks:
                        self.wfile.write(chunk)
                        self.wfile.flush()
                        chunk_index += 1
                        with self.server.state.condition:
                            self.server.state.h264_bytes_served += len(chunk)
                    if ended and chunk_index >= len(self.server.state.h264_chunks):
                        break
            except (BrokenPipeError, ConnectionResetError):
                pass
            finally:
                with self.server.state.condition:
                    self.server.state.h264_client_connections -= 1
                    self.server.state.condition.notify_all()
                print(
                    "LIVE_RAW_H264_CLIENT_DISCONNECTED "
                    f"chunks={chunk_index}",
                    flush=True,
                )
            return

        if self.path == "/stream.mjpg":
            self.send_response(HTTPStatus.OK)
            self.send_header(
                "Content-Type",
                "multipart/x-mixed-replace; boundary=helixframe",
            )
            self.send_no_cache_headers()
            self.end_headers()
            last_frame_number = -1
            try:
                while True:
                    with self.server.state.condition:
                        self.server.state.condition.wait_for(
                            lambda: (
                                self.server.state.jpeg is not None
                                and self.server.state.frame_number
                                != last_frame_number
                            ),
                            timeout=1.0,
                        )
                        jpeg = self.server.state.jpeg
                        frame_number = self.server.state.frame_number
                    if jpeg is None or frame_number == last_frame_number:
                        continue
                    self.wfile.write(b"--helixframe\r\n")
                    self.wfile.write(b"Content-Type: image/jpeg\r\n")
                    self.wfile.write(
                        f"Content-Length: {len(jpeg)}\r\n\r\n".encode()
                    )
                    self.wfile.write(jpeg)
                    self.wfile.write(b"\r\n")
                    self.wfile.flush()
                    last_frame_number = frame_number
            except (BrokenPipeError, ConnectionResetError):
                return

        self.send_error(HTTPStatus.NOT_FOUND)


def jpeg_reader(stdout, state: DisplayState) -> None:
    buffered = b""
    while True:
        chunk = stdout.read(65536)
        if not chunk:
            return
        buffered += chunk
        while True:
            start = buffered.find(b"\xff\xd8")
            if start < 0:
                buffered = buffered[-1:]
                break
            end = buffered.find(b"\xff\xd9", start + 2)
            if end < 0:
                buffered = buffered[start:]
                break
            jpeg = buffered[start : end + 2]
            buffered = buffered[end + 2 :]
            state.publish_jpeg(jpeg)


def decode_transport_packet(
    packet: bytes,
    dlsch: dict,
) -> tuple[bytes | None, str, int, int | None]:
    code_blocks = split6.split6_words_to_code_blocks(
        packet,
        int(dlsch["K"]),
        int(dlsch["C"]),
    )
    desegmented, cb_crc_ok = split6.desegment_code_blocks(
        code_blocks,
        int(dlsch["BGN"]),
        TRANSPORT_BLOCK_BITS + int(dlsch["L"]),
    )
    if not cb_crc_ok:
        return None, "code_block_crc", 0, None
    if (
        split6.crc24_remainder(
            desegmented,
            helix5g.CRC24A_TABLE,
        )
        != 0
    ):
        return None, "transport_crc", 0, None

    decoded = np.packbits(
        desegmented[:TRANSPORT_BLOCK_BITS],
        bitorder="little",
    ).tobytes()
    header = decoded[:32]
    (
        magic,
        version,
        flags,
        header_bytes,
        stream_id,
        frame_id,
        decoded_packet_index,
        packet_count_field,
        payload_length,
        reserved,
        payload_crc,
        header_crc,
    ) = struct.unpack("<4sBBHIIHHHHII", header)
    header_ok = (
        magic == b"HLVX"
        and version == 1
        and header_bytes == 32
        and stream_id == EXPECTED_STREAM_ID
        and packet_count_field == LIVE_PACKET_COUNT_FIELD
        and payload_length <= 1313
        and reserved == 0
        and header_crc == (zlib.crc32(header[:28]) & 0xFFFFFFFF)
        and bool(flags & 1)
        == (decoded_packet_index == 0 and not bool(flags & FEC_FLAG))
    )
    if not header_ok:
        return None, "hlvx_header", flags, decoded_packet_index
    payload = decoded[32 : 32 + payload_length]
    if payload_crc != (zlib.crc32(payload) & 0xFFFFFFFF):
        return None, "hlvx_payload_crc", flags, decoded_packet_index
    return payload, "", flags, decoded_packet_index


def main() -> int:
    args = parse_args()
    fifo_path = args.split6_fifo.resolve()
    session_dir = args.session_dir.resolve()
    if not fifo_path.exists():
        raise FileNotFoundError(fifo_path)
    if not stat.S_ISFIFO(fifo_path.stat().st_mode):
        raise ValueError(f"--split6-fifo is not a FIFO: {fifo_path}")
    if not 1024 <= args.http_port <= 65535:
        raise ValueError("--http-port must be in 1024..65535")
    if args.preview_width < 160 or args.preview_width > 1280:
        raise ValueError("--preview-width must be in 160..1280")
    if args.preview_fps < 1 or args.preview_fps > 30:
        raise ValueError("--preview-fps must be in 1..30")
    if args.keep_serving_seconds < 0:
        raise ValueError("--keep-serving-seconds must be nonnegative")
    session_dir.mkdir(parents=True, exist_ok=True)
    stop_file = args.stop_file.resolve() if args.stop_file is not None else None
    if stop_file is not None:
        stop_file.parent.mkdir(parents=True, exist_ok=True)
        if stop_file.exists():
            stop_file.unlink()
    ack_file = args.ack_file.resolve() if args.ack_file is not None else None
    if ack_file is not None:
        ack_file.parent.mkdir(parents=True, exist_ok=True)
        if ack_file.exists():
            ack_file.unlink()

    recovered_path = (
        session_dir / "fsoc_no_live_display_recovered.h264"
    )
    summary_path = (
        session_dir / "fsoc_no_live_display_receiver_summary.json"
    )
    ffmpeg_log_path = (
        session_dir / "fsoc_no_live_display_ffmpeg.log"
    )
    timing_path = session_dir / "rx_packet_timing.csv"
    for output in (recovered_path, summary_path, ffmpeg_log_path, timing_path):
        if output.exists():
            output.unlink()

    state = DisplayState(args.board_label)
    server = DisplayHTTPServer(
        (args.http_host, args.http_port),
        DisplayHandler,
        state,
    )
    server_thread = threading.Thread(
        target=server.serve_forever,
        name="mjpeg-http",
        daemon=True,
    )
    server_thread.start()
    print(
        "LIVE_DISPLAY_HTTP "
        f"url=http://192.168.3.1:{args.http_port}/",
        flush=True,
    )

    ffmpeg_command = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "warning",
        "-flags",
        "low_delay",
        "-f",
        "h264",
        "-r",
        "30",
        "-i",
        "pipe:0",
        "-an",
        "-vf",
        (
            f"fps={args.preview_fps},"
            f"scale={args.preview_width}:-2:flags=fast_bilinear"
        ),
        "-q:v",
        "5",
        "-c:v",
        "mjpeg",
        "-f",
        "image2pipe",
        "pipe:1",
    ]
    ffmpeg_log = None
    ffmpeg = None
    jpeg_thread = None
    if args.display_mode == "mjpeg":
        ffmpeg_log = ffmpeg_log_path.open("wb")
        ffmpeg = subprocess.Popen(
            ffmpeg_command,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=ffmpeg_log,
            bufsize=0,
            start_new_session=True,
        )
        if ffmpeg.stdin is None or ffmpeg.stdout is None:
            raise RuntimeError("FFmpeg pipes were not created")
        jpeg_thread = threading.Thread(
            target=jpeg_reader,
            args=(ffmpeg.stdout, state),
            name="jpeg-reader",
            daemon=True,
        )
        jpeg_thread.start()
    else:
        print(
            "LIVE_RAW_H264_HTTP_READY "
            f"url=http://192.168.3.1:{args.http_port}/live.h264",
            flush=True,
        )

    dlsch = helix5g.nrDLSCHInfo(
        TRANSPORT_BLOCK_BITS,
        TARGET_CODE_RATE,
    )
    state.message = "waiting for live Split-6 packets"
    print(f"LIVE_SPLIT6_FIFO_OPEN_WAIT path={fifo_path}", flush=True)
    received_digest = hashlib.sha256()
    final_flag_seen = False
    with (
        fifo_path.open("rb", buffering=0) as fifo_input,
        recovered_path.open("wb") as recovered_output,
        timing_path.open("w", encoding="utf-8", newline="") as timing_output,
    ):
        timing_output.write(
            "physical_packet_index,logical_packet_index,"
            "fifo_read_wall_ns,decode_done_wall_ns\n"
        )
        timing_output.flush()
        state.message = "receiving and decoding"
        physical_packet_index = 0
        next_logical_index = 0
        pending_packets: dict[int, tuple[bytes, int, int]] = {}
        fec_data_packets: dict[int, bytes] = {}
        fec_parity_groups: dict[int, dict[int, dict[str, object]]] = {}
        stream_complete = False

        def flush_ordered_packets() -> None:
            nonlocal next_logical_index, final_flag_seen, stream_complete
            while next_logical_index in pending_packets:
                (
                    ordered_payload,
                    ordered_flags,
                    ordered_physical_index,
                ) = pending_packets.pop(next_logical_index)
                recovered_output.write(ordered_payload)
                received_digest.update(ordered_payload)
                state.publish_h264(ordered_payload)
                if ffmpeg is not None:
                    assert ffmpeg.stdin is not None
                    ffmpeg.stdin.write(ordered_payload)
                state.accepted_packets += 1
                state.payload_bytes += len(ordered_payload)
                state.message = f"live logical packet {next_logical_index}"
                print(
                    "LIVE_UNIQUE_PACKET_PASS "
                    f"logical_index={next_logical_index} "
                    f"physical_index={ordered_physical_index} "
                    f"bytes={len(ordered_payload)}",
                    flush=True,
                )
                final_flag_seen |= bool(ordered_flags & 2)
                next_logical_index += 1
                if ordered_flags & 2:
                    stream_complete = True
                    if stop_file is not None:
                        stop_file.write_text(
                            f"final_logical_index={next_logical_index - 1}\n",
                            encoding="utf-8",
                        )
                        print(
                            "LIVE_RECEIVER_STOP_FILE_CREATED "
                            f"path={stop_file} "
                            f"final_logical_index={next_logical_index - 1}",
                            flush=True,
                        )
                    break

        def try_fec_recovery(group_start: int) -> None:
            group = fec_parity_groups.get(group_start, {})
            if not group:
                return
            reference = next(iter(group.values()))
            group_count = int(reference["group_count"])
            lengths = tuple(int(value) for value in reference["lengths"])
            final_position = int(reference["final_position"])
            for candidate in group.values():
                if (
                    int(candidate["group_count"]) != group_count
                    or tuple(int(value) for value in candidate["lengths"])
                    != lengths
                    or int(candidate["final_position"]) != final_position
                ):
                    state.hlvx_errors += 1
                    print(
                        f"LIVE_FEC_METADATA_CONFLICT group_start={group_start}",
                        flush=True,
                    )
                    return
            missing_positions = [
                position
                for position in range(group_count)
                if group_start + position not in fec_data_packets
            ]
            if not missing_positions:
                return
            if len(missing_positions) > 2:
                return

            residuals: dict[int, np.ndarray] = {}
            for parity_index, metadata in group.items():
                residual = np.asarray(metadata["symbol"], dtype=np.uint8).copy()
                for position in range(group_count):
                    data = fec_data_packets.get(group_start + position)
                    if data is None:
                        continue
                    vector = np.frombuffer(
                        data.ljust(DATA_PAYLOAD_BYTES, b"\x00"),
                        dtype=np.uint8,
                    )
                    if parity_index == 0:
                        residual ^= vector
                    else:
                        residual ^= gf_mul_vector(vector, position + 1)
                residuals[parity_index] = residual

            recovered_vectors: dict[int, np.ndarray] = {}
            if len(missing_positions) == 1:
                position = missing_positions[0]
                if 0 in residuals:
                    recovered_vectors[position] = residuals[0]
                elif 1 in residuals:
                    recovered_vectors[position] = gf_mul_vector(
                        residuals[1], gf_inverse(position + 1)
                    )
                else:
                    return
            else:
                if 0 not in residuals or 1 not in residuals:
                    return
                first_position, second_position = missing_positions
                first_coefficient = first_position + 1
                second_coefficient = second_position + 1
                first_vector = gf_mul_vector(
                    residuals[1]
                    ^ gf_mul_vector(residuals[0], second_coefficient),
                    gf_inverse(first_coefficient ^ second_coefficient),
                )
                recovered_vectors[first_position] = first_vector
                recovered_vectors[second_position] = residuals[0] ^ first_vector

            for position, vector in recovered_vectors.items():
                logical_index = group_start + position
                if logical_index in fec_data_packets:
                    continue
                payload = vector.tobytes()[: lengths[position]]
                flags = int(logical_index == 0)
                if position == final_position:
                    flags |= 2
                fec_data_packets[logical_index] = payload
                if logical_index >= next_logical_index:
                    pending_packets[logical_index] = (payload, flags, -1)
                state.fec_recovered_packets += 1
                print(
                    "LIVE_FEC_PACKET_RECOVERED "
                    f"logical_index={logical_index} "
                    f"group_start={group_start} "
                    f"erasures={len(missing_positions)}",
                    flush=True,
                )

        while True:
            packet = read_exact_packet(
                fifo_input,
                SPLIT6_BYTES_PER_PACKET,
            )
            if packet is None:
                break
            now = time.monotonic()
            fifo_read_wall_ns = time.time_ns()
            state.rx_packets += 1
            state.first_packet_time = state.first_packet_time or now
            state.last_packet_time = now
            payload, error, flags, logical_index = decode_transport_packet(
                packet,
                dlsch,
            )
            decode_done_wall_ns = time.time_ns()
            timing_output.write(
                f"{physical_packet_index},"
                f"{'' if logical_index is None else logical_index},"
                f"{fifo_read_wall_ns},{decode_done_wall_ns}\n"
            )
            timing_output.flush()
            if payload is None:
                if error == "code_block_crc":
                    state.code_block_crc_errors += 1
                elif error == "transport_crc":
                    state.transport_crc_errors += 1
                else:
                    state.hlvx_errors += 1
                state.message = f"physical packet {physical_packet_index}: {error}"
            else:
                if logical_index is None:
                    raise RuntimeError("validated HLVX packet has no sequence index")
                if flags & FEC_FLAG:
                    try:
                        fec_metadata = parse_fec_payload(payload, logical_index)
                    except ValueError as fec_error:
                        state.hlvx_errors += 1
                        print(
                            "LIVE_FEC_PACKET_REJECTED "
                            f"physical_index={physical_packet_index} "
                            f"reason={fec_error}",
                            flush=True,
                        )
                    else:
                        parity_index = int(fec_metadata["parity_index"])
                        group = fec_parity_groups.setdefault(logical_index, {})
                        if parity_index not in group:
                            group[parity_index] = fec_metadata
                        state.fec_parity_packets += 1
                        print(
                            "LIVE_FEC_PARITY_PASS "
                            f"group_start={logical_index} "
                            f"parity_index={parity_index} "
                            f"physical_index={physical_packet_index}",
                            flush=True,
                        )
                        try_fec_recovery(logical_index)
                        flush_ordered_packets()
                elif (
                    logical_index < next_logical_index
                    or logical_index in pending_packets
                ):
                    state.duplicate_packets += 1
                    state.message = (
                        f"duplicate logical packet {logical_index} "
                        f"physical {physical_packet_index}"
                    )
                    print(
                        "LIVE_DUPLICATE_PACKET "
                        f"logical_index={logical_index} "
                        f"physical_index={physical_packet_index}",
                        flush=True,
                    )
                else:
                    fec_data_packets[logical_index] = payload
                    pending_packets[logical_index] = (
                        payload,
                        flags,
                        physical_packet_index,
                    )
                    print(
                        "LIVE_PACKET_BUFFERED "
                        f"logical_index={logical_index} "
                        f"next_expected={next_logical_index} "
                        f"pending={len(pending_packets)}",
                        flush=True,
                    )
                    try_fec_recovery(
                        logical_index - (logical_index % FEC_GROUP_SIZE)
                    )
                    flush_ordered_packets()
                    # Higher-rate and long-duration streams can legitimately
                    # accumulate more than 128 out-of-order logical packets
                    # while late codeword variants/parity are still in flight.
                    # Keep a bounded 5 MiB-class window and decide only after
                    # all physical attempts reach FIFO EOF.
                    if len(pending_packets) > 4096:
                        state.sequence_errors += 1
                        state.message = "reorder buffer exceeded 4096 packets"
                        print("LIVE_REORDER_BUFFER_OVERFLOW", flush=True)
                        break
            physical_packet_index += 1
            if ack_file is not None:
                ack_file.write_text(
                    f"{physical_packet_index}\n",
                    encoding="utf-8",
                )
                print(
                    "LIVE_SPLIT6_ACK "
                    f"physical_packets={physical_packet_index} "
                    f"path={ack_file}",
                    flush=True,
                )
            if stream_complete:
                print(
                    "LIVE_FINAL_LOGICAL_PACKET_RECEIVED "
                    f"logical_packets={next_logical_index} "
                    f"physical_packets={state.rx_packets}",
                    flush=True,
                )
                break

        if pending_packets:
            highest_pending = max(pending_packets)
            missing = sum(
                index not in pending_packets
                for index in range(next_logical_index, highest_pending + 1)
            )
            state.fec_unrecoverable_groups = len(
                {
                    index - (index % FEC_GROUP_SIZE)
                    for index in range(next_logical_index, highest_pending + 1)
                    if index not in pending_packets
                }
            )
            state.sequence_errors += max(1, missing)
            print(
                "LIVE_SEQUENCE_INCOMPLETE "
                f"next_expected={next_logical_index} "
                f"highest_buffered={highest_pending} "
                f"pending={len(pending_packets)} missing={missing}",
                flush=True,
            )

    print(
        f"LIVE_SPLIT6_RECEIVER_EOF packets={state.rx_packets}",
        flush=True,
    )
    state.ended = True
    state.message = "stream ended; serving final frame"
    with state.condition:
        state.condition.notify_all()
    ffmpeg_return_code = None
    if ffmpeg is not None:
        assert ffmpeg.stdin is not None and ffmpeg.stdout is not None
        ffmpeg.stdin.close()
        try:
            ffmpeg_return_code = ffmpeg.wait(timeout=15)
        except subprocess.TimeoutExpired:
            # The preview transcode is auxiliary to byte-exact H.264 recovery.
            # On RFSoC's A53, a final burst can leave MJPEG frames queued even
            # after clean H.264 EOF. Interrupt the whole FFmpeg process group;
            # never let preview shutdown hold the RF role open.
            os.killpg(ffmpeg.pid, signal.SIGINT)
            try:
                ffmpeg_return_code = ffmpeg.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(ffmpeg.pid, signal.SIGKILL)
                ffmpeg_return_code = ffmpeg.wait(timeout=5)
        assert jpeg_thread is not None and ffmpeg_log is not None
        jpeg_thread.join(timeout=5)
        ffmpeg.stdout.close()
        ffmpeg_log.close()
    elapsed = (
        state.last_packet_time - state.first_packet_time
        if (
            state.first_packet_time is not None
            and state.last_packet_time is not None
        )
        else 0.0
    )
    display_pass = (
        state.frame_number > 0 and ffmpeg_return_code == 0
        if args.display_mode == "mjpeg"
        else (
            state.h264_client_ever_connected
            and state.h264_bytes_served > 0
        )
    )
    receiver_pass = (
        state.rx_packets > 0
        and state.accepted_packets > 0
        and state.sequence_errors == 0
        and final_flag_seen
        and display_pass
    )
    summary = {
        **state.snapshot(),
        "elapsed_seconds": elapsed,
        "payload_mbps": (
            state.payload_bytes * 8.0 / elapsed / 1_000_000.0
            if elapsed > 0
            else 0.0
        ),
        "final_flag_seen": final_flag_seen,
        "next_logical_index": next_logical_index,
        "pending_packets_at_eof": len(pending_packets),
        "failed_physical_attempts": (
            state.code_block_crc_errors
            + state.transport_crc_errors
            + state.hlvx_errors
        ),
        "ffmpeg_return_code": ffmpeg_return_code,
        "display_mode": args.display_mode,
        "display_pass": display_pass,
        "recovered_h264": str(recovered_path),
        "recovered_sha256": received_digest.hexdigest(),
        "http_url": f"http://192.168.3.1:{args.http_port}/",
        "receiver_pass": receiver_pass,
    }
    summary_path.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "LIVE_DISPLAY_SUMMARY "
        f"unique_packets={state.accepted_packets} "
        f"physical_packets={state.rx_packets} "
        f"duplicates={state.duplicate_packets} "
        f"sequence_errors={state.sequence_errors} "
        f"frames={state.frame_number} "
        f"payload_mbps={summary['payload_mbps']:.6f} "
        f"display_mode={args.display_mode} "
        f"ffmpeg_rc={ffmpeg_return_code}",
        flush=True,
    )
    print(
        f"FSOC_NO_LIVE_DISPLAY_RECEIVER_PASS={int(receiver_pass)}",
        flush=True,
    )

    stop_event = threading.Event()

    def stop_handler(signum, frame) -> None:
        stop_event.set()

    signal.signal(signal.SIGTERM, stop_handler)
    signal.signal(signal.SIGINT, stop_handler)
    if args.keep_serving_seconds == 0:
        while not stop_event.wait(1.0):
            pass
    else:
        stop_event.wait(args.keep_serving_seconds)
    server.shutdown()
    server.server_close()
    return 0 if receiver_pass else 2


if __name__ == "__main__":
    raise SystemExit(main())
