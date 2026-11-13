#!/usr/bin/env bash
# Two-board full-duplex video with SIC, both directions at the same time, run from a control PC that can ssh to both boards.
# usage: B0='ssh xilinx@<board0>' B1='ssh xilinx@<board1>' bash tools/run_pair.sh <run> [camera_s=60]
#   B0/B1: a command that runs "bash -s" input on that board (any ssh hops/keys you need, e.g.
#          B1='ssh user@hostA ssh -i ~/.ssh/key xilinx@192.168.3.1').  Board 0 = node 0, 1649.985 MHz; board 1 = node 1, 1650 MHz.
#   The settings validated on 2026-11-13 are the defaults of bidi_run.sh; IFG, HOLDOFF, REPEAT, AGC, RXN, BUSY set here are passed on.
#   While it runs, each board serves its received video at http://<board>:18750/ (see README: open it through an ssh tunnel).
# Afterwards each board's sic/bidi/<run>/ has source.h264 (its camera), received.h264 (the other board's camera),
# remote_source.h264 (copy of the other board's camera) and holes.py prints what was lost.
set -u
RUN=${1:?run name}; SEC=${2:-60}; : "${B0:?set B0}" "${B1:?set B1}"
W=/home/xilinx/claude_nlsg_20261110; D=$W/sic/bidi/$RUN
ENV=""; for v in IFG HOLDOFF REPEAT AGC RXN BUSY; do [ -n "${!v+x}" ] && ENV="$ENV $v=${!v}"; done
on() { local b=$1; shift; if [ "$b" = 0 ]; then $B0 bash -s <<< "$*"; else $B1 bash -s <<< "$*"; fi; }
for n in 0 1; do f=1649.985; [ $n = 1 ] && f=1650
  on $n "cd $W && bash set_clocks.sh >/dev/null; (setsid nohup env $ENV CAMDELAY=2 bash sic/bidi/bidi_run.sh $RUN $n $f $SEC > /tmp/bidi_$RUN.log 2>&1 < /dev/null &)" ; done
for n in 0 1; do on $n "for i in \$(seq 1 120); do grep -qa ONEWAY_READY $D/fdx.log 2>/dev/null && break; grep -qaE 'FATAL|FAILED' /tmp/bidi_$RUN.log && break; sleep 0.5; done; tail -2 /tmp/bidi_$RUN.log"; done
for n in 0 1; do on $n "touch $D/go" & done; wait; echo "GO $(date +%T)"
sleep $((SEC + 20))
for n in 0 1; do on $n "while pgrep -f 'build/[f]dx_bidi_claude' >/dev/null; do sleep 1; done; grep -a ONEWAY_RESULT $D/fdx.log | cut -c1-260"; done
put() { local b=$1 dst=$2 src=$3; { echo "cat > $dst; exit"; cat "$src"; } | if [ "$b" = 0 ]; then $B0 bash -s; else $B1 bash -s; fi; }
T=$(mktemp -d)
on 0 "cat $D/source.h264" > $T/s0.h264; on 1 "cat $D/source.h264" > $T/s1.h264
put 1 $D/remote_source.h264 $T/s0.h264; put 0 $D/remote_source.h264 $T/s1.h264; rm -rf "$T"
for n in 0 1; do echo "== board $n receives board $((1 - n))"; on $n "cd $W/sic/bidi && python3 holes.py $RUN"; done
