#!/usr/bin/env bash
# Claude 2026-11-11: start bridge + control service (both time-limited), wait until ready.
# usage: echo <sudo password> | bash run_stack.sh <bridge_seconds> <ctrl_seconds> [extra ctrl args]
# Outputs: bridge_run.log, ctrl_run.log.  Next: run a host program from ./host as a normal user.
cd /home/xilinx/claude_nlsg_20261110 || exit 1
read -r PW
BIT="${HELIX_BIT:-helix_nlsg.bit}"   # set HELIX_BIT=helix_nlsg_s6.bit for the split-6 build
PY="env BOARD=RFSoC4x2 XILINX_XRT=/usr HELIX_BRIDGE_TIMING=${HELIX_BRIDGE_TIMING:-} HELIX_CTRL_TIMING=${HELIX_CTRL_TIMING:-} /usr/local/share/pynq-venv/bin/python3 -B -u"
if ps -eo args | grep -E 'nl_tap_bridge|helix_ctrl_service' | grep -v grep >/dev/null; then echo STACK_ALREADY_RUNNING; exit 1; fi
B=$1; C=$2; shift 2
(echo "$PW" | sudo -S -p '' setsid nohup $PY nl_tap_bridge.py "$BIT" --no-download --seconds "$B" > bridge_run.log 2>&1 < /dev/null &)
for i in $(seq 1 30); do grep -q BRIDGE_READY bridge_run.log 2>/dev/null && break; sleep 1; done
grep -q BRIDGE_READY bridge_run.log || { echo BRIDGE_FAILED; cat bridge_run.log; exit 1; }
(echo "$PW" | sudo -S -p '' setsid nohup $PY helix_ctrl_service.py "$BIT" --seconds "$C" "$@" > ctrl_run.log 2>&1 < /dev/null &)
for i in $(seq 1 40); do grep -qE 'CONTROL_READY|Traceback' ctrl_run.log 2>/dev/null && break; sleep 1; done
grep -q CONTROL_READY ctrl_run.log || { echo CTRL_FAILED; cat ctrl_run.log; exit 1; }
grep -E 'router status' ctrl_run.log; echo STACK_READY
