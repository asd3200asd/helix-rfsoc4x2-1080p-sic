#!/usr/bin/env bash
# Claude 2026-11-13: one board of the two-board full-duplex video test.
# usage: bash sic/bidi/bidi_run.sh <run> <node 0|1> <freq_MHz> <camera_s>        then, on BOTH boards: touch sic/bidi/<run>/go
#   1. reload helix_nlsg_sic3.bit + services, start fdx_bidi_claude (both, filler bursts until the first video byte) reading
#      sic/bidi/<run>/in.fifo and writing the received video to <run>/received.h264; start the live viewer; print ONEWAY_READY.
#   2. when <run>/go appears: orchestrate2.py calibrates the SIC on the filler (CAL s after go) and switches it on 0.1 s later,
#      the camera (1080p30 H.264, -c copy, camera_s seconds) starts CAMDELAY s after go -> source.h264 + in.fifo.
#      fdx_bidi_claude is stopped 15 s after the camera ends.
# Environment (defaults = settings validated on 2026-11-13):
#   IFG=250000 tx_ifg | HOLDOFF=58980 slot hold-off in 4.07 ns beats (240 us; the bit's built-in value is 196 us) | REPEAT=6 copies
#   per TB, spread over consecutive bursts | AGC=1200 receive AGC Vref while the SIC is on (or off; 800 is the library default) | CAL=0.3 | CAMDELAY=2
#   RXN=1000000 packets per receive trigger (large, so a control-path hiccup does not stop reception)
#   VIEW=1 browser view http://<board>:18750/ | VIEW_DP=1 also full screen on the board's DisplayPort monitor | MODE=rx|tx (diagnostics)
set -u
N=$1; NODE=$2; F=$3; SEC=$4
IFG=${IFG:-250000}; HOLDOFF=${HOLDOFF:-58980}; REPEAT=${REPEAT:-6}; AGC=${AGC:-1200}; CAL=${CAL:-0.3}; CAMDELAY=${CAMDELAY:-2}
W=/home/xilinx/claude_nlsg_20261110; D=$W/sic/bidi/$N; PY=/usr/local/share/pynq-venv/bin/python3
cd $W || exit 1
sudo pkill -KILL -f 'sic/bidi/[^ ]*/go'; sudo pkill -KILL -f '[f]fmpeg -nostdin'; sudo pkill -KILL -f 'sic/bidi/[v]iew_server.py'; sudo pkill -f '[f]fmpeg .*-f mpjpeg'; sudo pkill -f '[t]ail -c 0 -F .*received.h264'; sudo pkill -f '[f]fplay -hide_banner'
for p in $(pgrep -f '[f]dx_bidi_claude|[f]dx_sic_claude|[o]rchestrate'); do sudo kill -INT $p 2>/dev/null; done; sleep 1
for i in $(seq 1 20); do pgrep -f 'build/[f]dx_|[o]rchestrate' >/dev/null || break; sleep 0.25; done; sudo pkill -KILL -f 'build/[f]dx_|[o]rchestrate'   # an fdx still running keeps the UDP port
for p in $(pgrep -f 'python3 .*([h]elix_ctrl_service|[n]l_tap_bridge)'); do sudo kill -TERM $p 2>/dev/null; done
for i in $(seq 1 20); do pgrep -f '[h]elix_ctrl_service|[n]l_tap_bridge' >/dev/null || break; sleep 0.5; done
sudo env BOARD=RFSoC4x2 XILINX_XRT=/usr $PY -B -u stepA_load_probe_sic3.py --load 2>&1 | grep -E 'LOADED|rror'
sudo env HELIX_DAC_VOP=40500 HELIX_CORE=1 HELIX_BUSY=${BUSY-1} HELIX_BIT=helix_nlsg_sic3.bit bash -c 'echo x | bash run_stack.sh "$@"' _ $((SEC + 400)) $((SEC + 395)) --skip-clocks 2>&1 | tail -1
sudo rm -rf "$D"; mkdir -p "$D"; chmod 777 "$D"; mkfifo "$D/in.fifo"; chmod 666 "$D/in.fifo"; : > "$D/received.h264"
(cd $W/host && setsid nohup bash -c "exec 3<>'$D/in.fifo'; exec env HELIX_NODE=$NODE ONEWAY_MODE=${MODE:-both} ONEWAY_RAW=1 ONEWAY_PREFILL=1 ONEWAY_RXN=${RXN:-1000000} ONEWAY_REPEAT=$REPEAT ONEWAY_PKTLOG=$D/pkt.bin HELIX_FREQ_MHZ=$F timeout -s INT 900 build/fdx_bidi_claude $IFG 32 256 < '$D/in.fifo' > '$D/received.h264' 2> '$D/fdx.log'" > /dev/null 2>&1 < /dev/null &)
for i in $(seq 1 40); do grep -qaE 'ONEWAY_READY|FATAL' "$D/fdx.log" 2>/dev/null && break; sleep 0.5; done
if [ "${VIEW:-1}" = 1 ]; then (setsid nohup $PY sic/bidi/view_server.py "$D" 18750 $((SEC + 300)) > "$D/view.log" 2>&1 < /dev/null &); fi
if [ "${VIEW_DP:-0}" = 1 ]; then
  AUTH=$(ps -C Xorg -o args= | sed -n 's/.* -auth \([^ ]*\).*/\1/p' | head -1)
  (sudo setsid nohup bash -c "tail -c 0 -F '$D/received.h264' 2>/dev/null | env DISPLAY=:0 XAUTHORITY=$AUTH SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software timeout $((SEC + 120)) nice -n 19 ffplay -hide_banner -loglevel error -fs -an -window_title 'HELIX received' -fflags nobuffer -flags low_delay -framedrop -probesize 65536 -f h264 -framerate 30 -vf format=bgra -i pipe:0" > "$D/dp.log" 2>&1 < /dev/null &)
fi
(sudo setsid nohup bash -c "while [ ! -e '$D/go' ]; do sleep 0.02; done; date +%s.%N > '$D/go_at'
  env HOLDOFF=$HOLDOFF $PY sic/bidi/orchestrate2.py '$D' '$D/fdx.log' $CAL $(awk "BEGIN{print $CAL+0.1}"):1:$AGC $((SEC + 25)) > '$D/orch.log' 2>&1 &
  sleep $CAMDELAY
  ffmpeg -nostdin -hide_banner -loglevel error -f v4l2 -input_format h264 -video_size 1920x1080 -framerate 30 -i /dev/video0 -c copy -t $SEC -f h264 pipe:1 2>'$D/camera.log' | tee '$D/source.h264' > '$D/in.fifo'
  date +%s.%N > '$D/camera_end'; sleep 15; pkill -INT -f 'build/[f]dx_bidi_claude'" > /dev/null 2>&1 < /dev/null &)
grep -aE 'READY|FATAL' "$D/fdx.log" | cut -c1-70; date +%T.%N | cut -c1-12
