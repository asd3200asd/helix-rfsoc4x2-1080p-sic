#!/usr/bin/env bash
# usage: ab.sh <label> [ENV=val ...] : print previous result, arm receiver (oneway2 raw, 1649.985) with extra env
cd /home/xilinx/claude_nlsg_20261110
for i in $(seq 1 40); do pgrep -f '[b]uild/oneway2' >/dev/null || break; sleep 1; done
[ -s dual_rx.err ] && echo "PREV $(cat /tmp/ab_label 2>/dev/null): $(grep ONEWAY_RESULT dual_rx.err | sed 's/.*rx_packets/rx_packets/' | cut -c1-75)"
[ "$1" = "-" ] && exit 0
L=$1; shift; : > dual_rx.err; echo "$L" > /tmp/ab_label
BIN=oneway2_split6_claude bash dual_board.sh recv 1649.985 /tmp/sw_out.bin 26 ONEWAY_RAW=1 "$@" | tail -1
