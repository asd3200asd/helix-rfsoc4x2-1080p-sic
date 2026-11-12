#!/usr/bin/env bash
# usage: rx_only.sh <name> <rx_freq_MHz> <timeout_s> [binary=fdx_sic_claude]   (receive only, packet log in sic/fdx/<name>/pkt.bin)
cd /home/xilinx/claude_nlsg_20261110 || exit 1
N=$1; mkdir -p sic/fdx/$N; rm -f sic/fdx/$N/*
(cd host && setsid nohup env ONEWAY_MODE=rx ONEWAY_RAW=1 ONEWAY_PKTLOG=../sic/fdx/$N/pkt.bin HELIX_FREQ_MHZ=$2 timeout -s INT $3 build/${4:-fdx_sic_claude} 180000 32 256 > /dev/null 2> ../sic/fdx/$N/fdx.log < /dev/null &)
for i in $(seq 1 20); do grep -qaE 'ONEWAY_READY|FATAL|rror' sic/fdx/$N/fdx.log 2>/dev/null && break; sleep 0.5; done; grep -aE 'ONEWAY_READY|FATAL|rror' sic/fdx/$N/fdx.log | cut -c1-60
