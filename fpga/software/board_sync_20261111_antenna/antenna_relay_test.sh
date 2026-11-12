#!/usr/bin/env bash
# usage: echo <pw> | bash antenna_relay_test.sh <freq_MHz> <infile> <outfile> [env assignments for the relay...]
read -r PW; F="$1"; IN="$2"; OUT="$3"; shift 3
cd /home/xilinx/claude_nlsg_20261110
for i in $(seq 1 40); do ps -eo args | grep -E '[n]l_tap_bridge.py|[h]elix_ctrl_service.py' >/dev/null || break; sleep 1; done
echo "$PW" | sudo -S -p '' env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B -u stepA_load_probe_s6v2.py --load >/dev/null 2>&1
echo "$PW" | HELIX_DAC_VOP=40500 HELIX_CORE=1 HELIX_BUSY=1 HELIX_BIT=helix_nlsg_s6v2.bit bash run_stack.sh 45 40 --skip-clocks >/dev/null
cd host; env HELIX_FREQ_MHZ=$F "$@" timeout 35 build/stream_relay_split6_claude 180000 32 256 < "$IN" 2>&1 > "$OUT" | grep -E 'STREAM_RESULT|FAULT|BURST|FATAL|timeout' | cut -c1-260
cmp -s "$IN" "$OUT" && echo FILE_EXACT || echo FILE_DIFFERENT
