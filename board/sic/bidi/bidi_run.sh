#!/usr/bin/env bash
# Claude 2026-11-13: one board of the two-board full-duplex video test.
# usage: bash sic/bidi/bidi_run.sh <run> <node 0|1> <freq_MHz> <camera_s> [cal_s=1]      then, on BOTH boards: touch sic/bidi/<run>/go
#   1. reload helix_nlsg_sic3.bit + services, start fdx_bidi_claude (both, R=3, tx_ifg 200000) reading sic/bidi/<run>/in.fifo,
#      print ONEWAY_READY and return.
#   2. when <run>/go appears: camera (1080p30 H.264, -c copy, camera_s seconds) -> source.h264 + in.fifo, and orchestrate2.py
#      (SIC calibration cal_s after go, SIC on 0.1 s later with AGC 800, off at camera_s+15).
#   15 s after the camera ends fdx_bidi_claude is stopped (SIGINT).  Received remote video: <run>/received.h264.  MODE=rx|tx overrides ONEWAY_MODE (diagnostics).
#   IFG=<tx_ifg> (default 200000); REPEAT=<copies per TB> (default 3); AGC=<Vref>|off (receive AGC while SIC is on, default 800); HOLDOFF=<beats of 4.07 ns> overrides the sic3 slot hold-off (built-in 196 us = 48170).
set -u
N=$1; NODE=$2; F=$3; SEC=$4; CAL=${5:-1}
W=/home/xilinx/claude_nlsg_20261110; D=$W/sic/bidi/$N
cd $W || exit 1
sudo pkill -KILL -f 'sic/bidi/[^ ]*/go'; sudo pkill -KILL -f '[f]fmpeg -nostdin'   # waiters / camera left from an earlier run
for p in $(pgrep -f '[f]dx_bidi_claude|[f]dx_sic_claude|[o]rchestrate|[f]fmpeg -nostdin'); do sudo kill -INT $p 2>/dev/null; done; sleep 1
for p in $(pgrep -f 'python3 .*([h]elix_ctrl_service|[n]l_tap_bridge)'); do sudo kill -TERM $p 2>/dev/null; done
for i in $(seq 1 20); do pgrep -f '[h]elix_ctrl_service|[n]l_tap_bridge' >/dev/null || break; sleep 0.5; done
sudo env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B -u stepA_load_probe_sic3.py --load 2>&1 | grep -E 'LOADED|rror'
sudo env HELIX_DAC_VOP=40500 HELIX_CORE=1 HELIX_BUSY=1 HELIX_BIT=helix_nlsg_sic3.bit bash -c 'echo x | bash run_stack.sh "$@"' _ $((SEC + 400)) $((SEC + 395)) --skip-clocks 2>&1 | tail -1
sudo rm -rf "$D"; mkdir -p "$D"; chmod 777 "$D"; mkfifo "$D/in.fifo"; chmod 666 "$D/in.fifo"
(cd $W/host && setsid nohup bash -c "exec 3<>'$D/in.fifo'; exec env HELIX_NODE=$NODE ONEWAY_MODE=${MODE:-both} ONEWAY_RAW=1 ONEWAY_RXN=32768 ONEWAY_REPEAT=${REPEAT:-3} ONEWAY_PKTLOG=$D/pkt.bin HELIX_FREQ_MHZ=$F timeout -s INT 900 build/fdx_bidi_claude ${IFG:-200000} 32 256 < '$D/in.fifo' > '$D/received.h264' 2> '$D/fdx.log'" > /dev/null 2>&1 < /dev/null &)
for i in $(seq 1 40); do grep -qaE 'ONEWAY_READY|FATAL' "$D/fdx.log" 2>/dev/null && break; sleep 0.5; done
(sudo setsid nohup bash -c "while [ ! -e '$D/go' ]; do sleep 0.02; done; date +%s.%N > '$D/go_at'
  env HOLDOFF=${HOLDOFF:-} /usr/local/share/pynq-venv/bin/python3 sic/bidi/orchestrate2.py '$D' '$D/fdx.log' $CAL $(awk "BEGIN{print $CAL+0.1}"):1:${AGC:-800} $((SEC + 15)) > '$D/orch.log' 2>&1 &
  ffmpeg -nostdin -hide_banner -loglevel error -f v4l2 -input_format h264 -video_size 1920x1080 -framerate 30 -i /dev/video0 -c copy -t $SEC -f h264 pipe:1 2>'$D/camera.log' | tee '$D/source.h264' > '$D/in.fifo'
  date +%s.%N > '$D/camera_end'; sleep 15; pkill -INT -f 'build/[f]dx_bidi_claude'" > /dev/null 2>&1 < /dev/null &)
grep -aE 'READY|FATAL' "$D/fdx.log" | cut -c1-70; date +%T.%N | cut -c1-12
