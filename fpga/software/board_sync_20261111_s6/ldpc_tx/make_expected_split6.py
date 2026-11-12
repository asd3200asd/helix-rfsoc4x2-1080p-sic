# Claude 2026-11-11: expected RX Split-6 output (1408 B per TB: C code blocks of K bits, LSB-first) for a transport block file.
# usage: python3 make_expected_split6.py <tb_file (1345 B each)> <out_file>
import sys, numpy as np
sys.path[:0] = ["/home/xilinx/helix_py3gpp", "/home/xilinx/helix_webcam_capture/py3gpp_bench_stage"]
import fsoc_no_py3gpp_nre73_encoder_bitexact_benchmark as h
tb = np.fromfile(sys.argv[1], np.uint8).reshape(-1, 1345)
bgn = int(h.nrDLSCHInfo(1345 * 8, 0.4785)["BGN"]); out = bytearray()
for t in tb:
    cb = np.asarray(h.transport_block_to_code_blocks(t, bgn)).copy(); cb[cb < 0] = 0      # (K, C), fillers -> 0
    out += np.packbits(cb.T.reshape(-1).astype(np.uint8), bitorder="little").tobytes()
open(sys.argv[2], "wb").write(out); print("TBs", tb.shape[0], "bytes", len(out), "per TB", len(out) // tb.shape[0])
