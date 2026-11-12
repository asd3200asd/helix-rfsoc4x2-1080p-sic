#!/usr/bin/env bash
# Claude 2026-11-11: antenna quick test.  usage: echo <pw> | bash antenna_sweep.sh <vop_uA> <dsa_dB> <freq_MHz> [freq ...]
# Reloads the bit before every frequency (a failed burst can leave the Stream Manager stuck), then 3 x 32 TBs.
read -r PW; VOP="$1"; DSA="$2"; shift 2
cd /home/xilinx/claude_nlsg_20261110
T=/home/xilinx/helix_webcam_capture/py3gpp_bench_stage/fsoc_no_helix_nre73_h264_transport_blocks.bin; E=ldpc_tx/h264_535_expected_split6.bin
for F in "$@"; do
  for i in $(seq 1 40); do ps -eo args | grep -E '[n]l_tap_bridge.py|[h]elix_ctrl_service.py' >/dev/null || break; sleep 1; done
  echo "$PW" | sudo -S -p '' env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B -u stepA_load_probe_s6v2.py --load >/dev/null 2>&1
  echo "$PW" | HELIX_DAC_VOP=$VOP HELIX_ADC_DSA=$DSA HELIX_CORE=1 HELIX_BUSY=1 HELIX_BIT=helix_nlsg_s6v2.bit bash run_stack.sh 14 9 --skip-clocks >/dev/null
  for k in 1 2 3; do r=$(cd host && HELIX_FREQ_MHZ=$F timeout 5 build/stress_split6_claude $T ../$E 32 1 180000 0 128 32 2>&1 | grep STRESS | sed 's/.*sent=/sent=/; s/ bursts_all_ok.*//'); echo "F=$F VOP=$VOP DSA=$DSA :: $r"; done
done
