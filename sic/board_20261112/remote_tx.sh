#!/usr/bin/env bash
# Claude 2026-11-12 (SIC test, remote board): initialise the transmitter now, start sending remote_50000tb.bin after <delay_s>.
# usage: remote_tx.sh <delay_s> <freq_MHz> <repeat> <tag>      log: sic/fdx/remote_tx_<tag>.log
cd /home/xilinx/claude_nlsg_20261110/host || exit 1
D=$1; F=$2; R=$3; TAG=$4; L=../sic/fdx/remote_tx_$TAG.log; rm -f $L
(setsid nohup bash -c "(sleep $D; cat ../sic/fdx/remote_50000tb.bin) | env ONEWAY_MODE=tx ONEWAY_REPEAT=$R HELIX_FREQ_MHZ=$F build/oneway2_split6_claude 180000 32 256 > /dev/null 2> $L" > /dev/null 2>&1 < /dev/null &)
for i in $(seq 1 30); do grep -qaE 'ONEWAY_READY|FATAL|rror' $L 2>/dev/null && break; sleep 0.5; done
grep -aE 'ONEWAY_READY|FATAL|rror' $L | head -2 | cut -c1-120
