# offline: Codex TB file -> encoder -> compare with Codex Split-7.3 file (no hardware)
import sys, numpy as np
sys.path[:0] = ["/home/xilinx/helix_py3gpp", "/home/xilinx/helix_webcam_capture/py3gpp_bench_stage"]
import fsoc_no_py3gpp_nre73_encoder_bitexact_benchmark as h
tb = np.fromfile("/home/xilinx/helix_webcam_capture/py3gpp_bench_stage/fsoc_no_helix_nre73_h264_transport_blocks.bin", np.uint8).reshape(-1, 1345)
ref = np.fromfile("/home/xilinx/fsoc_no_helix_nre73_h264_split73.i16le", "<i2").reshape(-1, 1316)
print("TBs", tb.shape, "ref", ref.shape)
bgn = int(h.nrDLSCHInfo(1345*8, 0.4785)["BGN"]); enc = h.CachedLDPCEncoder(h.transport_block_to_code_blocks(np.zeros(1345, np.uint8), bgn), bgn)
ok = 0
for i in (0, 1, 2, 100, tb.shape[0]-1):
    w = h.encoded_bits_to_i16_words(h.encode_transport_block(tb[i], bgn, 21054, enc)); ok += int((w == ref[i]).all())
print("ENCODER_CHECK exact", ok, "of 5")
