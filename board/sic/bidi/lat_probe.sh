#!/bin/bash
# usage: lat_probe.sh <run> <seconds>: every 50 ms "t source_size received_size" -> /dev/shm/lat_<run>.txt
D=/home/xilinx/claude_nlsg_20261110/sic/bidi/$1; O=/dev/shm/lat_$1.txt; : > $O; e=$(( $(date +%s) + $2 ))
while [ $(date +%s) -lt $e ]; do echo "$(date +%s.%N) $(stat -c %s $D/source.h264 2>/dev/null || echo 0) $(stat -c %s $D/received.h264 2>/dev/null || echo 0)" >> $O; sleep 0.05; done
