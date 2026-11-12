#!/usr/bin/env bash
# usage: run_fdx.sh <run_name> <freq_MHz> <local_repeat> <fdx_timeout_s> <cal_at_s> <schedule> <end_s>
# Starts the full-duplex program (local TX of local_20000tb.bin + RX log) and the SIC timeline in the background.
cd /home/xilinx/claude_nlsg_20261110/sic/fdx || exit 1
N=$1; F=$2; R=$3; TMO=$4; CAL=$5; SCH=$6; END=$7; mkdir -p "$N"; rm -f "$N"/pkt.bin "$N"/fdx.log "$N"/events.json
(cd ../../host && setsid nohup env ONEWAY_MODE=both ONEWAY_RAW=1 ONEWAY_REPEAT=$R ONEWAY_PKTLOG=../sic/fdx/$N/pkt.bin HELIX_FREQ_MHZ=$F timeout -s INT $TMO build/fdx_sic_claude 180000 32 256 < ../sic/fdx/local_20000tb.bin > /dev/null 2> ../sic/fdx/$N/fdx.log &)
(sudo setsid nohup /usr/local/share/pynq-venv/bin/python3 orchestrate.py "$N" "$N/fdx.log" "$CAL" "$SCH" "$END" > "$N/orch.log" 2>&1 < /dev/null &)
echo STARTED $N $(date +%T)
