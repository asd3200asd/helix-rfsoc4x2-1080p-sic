# Claude 2026-11-11: reference cases for ldpc_tx_enc from py3gpp (via Codex's helper module), BG1 forced.
# Output cases.bin: per case  u32 A, u32 G, u32 Qm, TB bytes (A/8), reference rate-matched bits packed LSB-first (ceil(G/8) bytes)
import sys, struct, numpy as np
sys.path[:0] = ["/home/xilinx/helix_py3gpp", "/home/xilinx/helix_webcam_capture/py3gpp_bench_stage"]
import fsoc_no_py3gpp_nre73_encoder_bitexact_benchmark as h
MOD = {2: "QPSK", 4: "16QAM", 6: "64QAM", 8: "256QAM"}
cases = [(10760, 21054, 2), (5000, 12000, 2), (8424, 16000, 2), (20000, 44000, 2), (30000, 40002, 2), (6000, 20000, 2), (4000, 15000, 2),
         (10760, 21056, 4), (24000, 60000, 6), (12000, 30000, 4), (40000, 88000, 8), (3848, 8000, 2), (9000, 34000, 2), (16896, 30000, 2)]
rng = np.random.default_rng(20261111); out = bytearray()
for A, G, Qm in cases:
    B = A + 24; C = 1 if B <= 8448 else -(-B // 8424)
    A = (B // (8 * C)) * 8 * C - 24          # Codex's fast CRC needs whole-byte code blocks
    tb = rng.integers(0, 256, A // 8, dtype=np.uint8)
    cb = h.transport_block_to_code_blocks(tb, 1); enc = h.CachedLDPCEncoder(cb, 1)
    bits = np.asarray(h.nrRateMatchLDPC(enc.encode(cb), G, 0, MOD[Qm], 1), dtype=np.uint8).reshape(-1)
    assert bits.size == G, (bits.size, G)
    out += struct.pack("<III", A, G, Qm) + tb.tobytes() + np.packbits(bits, bitorder="little").tobytes()
    print("case A=%d G=%d Qm=%d C=%d K=%d Zc=%d" % (A, G, Qm, cb.shape[1], cb.shape[0], enc.z_c))
open("cases.bin", "wb").write(out); print("cases", len(cases), "bytes", len(out))
