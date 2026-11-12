#!/usr/bin/env bash
# Claude 2026-11-12: reload sic2, start one full-duplex run in the background, wait for READY.
# usage: HOLDOFF=<beats> [FEED_TB=16 FEED_MS=100 | FULL=1] [SSB=...] bash sic/fdx/go.sh <name> <local_repeat> <timeout_s> <schedule> <end_s>
cd /home/xilinx/claude_nlsg_20261110 || exit 1
SICBIT=${SICBIT:-sic2} bash sic/fdx/pc_reload.sh 1500 < /dev/null 2>&1 | tail -1
S=run_fdx3.sh; [ -n "$FULL" ] && S=run_fdx2.sh; [ -n "$SCRIPT" ] && S=$SCRIPT   # SCRIPT=run_fdx4.sh keeps the received data; CAL=<s> calibration time (default 28)
(LD=25 setsid nohup bash sic/fdx/$S "$1" 1649.985 "$2" "$3" ${CAL:-28} "$4" "$5" > /tmp/go_start.log 2>&1 < /dev/null &)
for i in $(seq 1 30); do grep -qaE 'ONEWAY_READY|FATAL' sic/fdx/$1/fdx.log 2>/dev/null && break; sleep 0.5; done
grep -aE 'READY|FATAL' sic/fdx/$1/fdx.log | cut -c1-60; date +%T
