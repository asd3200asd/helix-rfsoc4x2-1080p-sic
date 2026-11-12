#!/usr/bin/env bash
# Claude 2026-11-12 test helper. usage: sw.sh <rx_freq|-> [timeout_s]  : print the previous receive result, then arm the receiver (oneway2, raw) at <rx_freq>
#                                       sw.sh both <freq>              : single-board self test (tx+rx on this board, original program), prints the result
cd /home/xilinx/claude_nlsg_20261110
if [ "$1" = both ]; then cd host; env ONEWAY_MODE=both ONEWAY_RAW=1 HELIX_FREQ_MHZ=$2 timeout 40 build/oneway_split6_claude 180000 32 256 < ../live4k/cam4k_8s.h264 2>&1 >/tmp/both_out.bin | grep -E 'ONEWAY_RESULT|WARN|FATAL' | sed 's/.*rx_packets/rx_packets/' | cut -c1-110; exit 0; fi
for i in $(seq 1 40); do pgrep -f '[o]neway2_split6' >/dev/null || break; sleep 1; done
[ -f dual_rx.err ] && echo "PREV $(cat /tmp/sw_freq 2>/dev/null): $(grep ONEWAY_RESULT dual_rx.err | sed 's/.*rx_packets/rx_packets/' | cut -c1-90)"
[ "$1" = "-" ] && exit 0
: > dual_rx.err; echo "$1" > /tmp/sw_freq
BIN=oneway2_split6_claude bash dual_board.sh recv "$1" /tmp/sw_out.bin ${2:-20} ONEWAY_RAW=1 | tail -1
