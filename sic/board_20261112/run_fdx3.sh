#!/usr/bin/env bash
# usage: run_fdx3.sh (run_fdx2 with a throttled local feed: FEED_TB transport blocks every FEED_MS ms)
# usage: run_fdx2.sh (same as run_fdx.sh but schedule items may be t:sic:agc, see orchestrate2.py)
# usage: run_fdx.sh <run_name> <freq_MHz> <local_repeat> <fdx_timeout_s> <cal_at_s> <schedule> <end_s>
# LD=<s> in the environment delays the local transmit data by that many seconds after initialisation.
# Starts the full-duplex program (local TX of local_20000tb.bin + RX log) and the SIC timeline in the background.
cd /home/xilinx/claude_nlsg_20261110/sic/fdx || exit 1
N=$1; F=$2; R=$3; TMO=$4; CAL=$5; SCH=$6; END=$7; mkdir -p "$N"; rm -f "$N"/pkt.bin "$N"/fdx.log "$N"/events.json
(cd ../../host && LD=${LD:-0} setsid nohup bash -c 'exec env ONEWAY_MODE=both ONEWAY_RAW=1 ONEWAY_REPEAT='$R' ONEWAY_PKTLOG=../sic/fdx/'$N'/pkt.bin HELIX_FREQ_MHZ='$F' ${TXF:+HELIX_TX_FREQ_MHZ=$TXF} ${SSB:+HELIX_SSB_FILE=$SSB} timeout -s INT '$TMO' build/fdx_sic_claude ${IFG:-180000} 32 256 < <(sleep ${LD:-0}; exec python3 ../sic/fdx/feed_sparse.py ../sic/fdx/local_20000tb.bin 0 ${FEED_TB:-16} ${FEED_MS:-100}) > /dev/null 2> ../sic/fdx/'$N'/fdx.log' > /dev/null 2>&1 < /dev/null &)
(sudo env HOLDOFF=${HOLDOFF:-} setsid nohup /usr/local/share/pynq-venv/bin/python3 orchestrate2.py "$N" "$N/fdx.log" "$CAL" "$SCH" "$END" > "$N/orch.log" 2>&1 < /dev/null &)
echo STARTED $N $(date +%T)
