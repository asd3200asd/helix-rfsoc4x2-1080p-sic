#!/usr/bin/env bash
# Claude 2026-11-11: same as run_live_webcam_udp.sh, but the recovered (RX) H.264 is decoded on the board and drawn on the
# DisplayPort monitor through /dev/fb0 (1280x720 centred on the 1920x1080 desktop).  Receiver runs in raw-h264 mode.
# Prereq: run_stack.sh with enough seconds.  Run as normal user (needs group video):
#   nohup setsid bash live/run_live_dp.sh <session_dir> <seconds> [bitrate_kbps=600] [fps=10] [workers=3] > <log> 2>&1 &
# Outputs in session dir: producer.log relay.log receiver.log display.log, source/recovered .h264; last line of the log
# is LIVE_SOURCE_RECOVERED_EXACT_PASS/FAIL.  Stop early: pkill -f claude_live_webcam_split73_fifo_producer
set -u
SESSION="$1"; DURATION="$2"; KBPS="${3:-600}"; FPS="${4:-10}"; WORKERS="${5:-3}"; PORT=8091
STAGE=/home/xilinx/helix_webcam_capture/py3gpp_bench_stage; HERE=/home/xilinx/claude_nlsg_20261110
PYTHON=/usr/local/share/pynq-venv/bin/python3
mkdir -p "$SESSION"; TX="$SESSION/tx_split73.fifo"; RX="$SESSION/rx_split6.fifo"; rm -f "$TX" "$RX"; mkfifo "$TX" "$RX"
PYTHONPATH="/home/xilinx/helix_py3gpp:$STAGE" "$PYTHON" -B -u "$HERE/live/codex_fec_receiver_20261014.py" --board-label nlsg_udp \
  --display-mode raw-h264 --split6-fifo "$RX" --session-dir "$SESSION" --http-port "$PORT" --keep-serving-seconds 3 >"$SESSION/receiver.log" 2>&1 &
rcv=$!
for i in $(seq 1 20); do grep -q LIVE_DISPLAY_HTTP "$SESSION/receiver.log" 2>/dev/null && break; sleep 0.5; done
ffmpeg -hide_banner -nostdin -loglevel info -stats_period 10 -fflags nobuffer -flags low_delay -probesize 400k -analyzeduration 0 \
  -f h264 -i "http://127.0.0.1:$PORT/live.h264" -vsync passthrough -pix_fmt rgb565le -f fbdev -xoffset 320 -yoffset 180 /dev/fb0 >"$SESSION/display.log" 2>&1 &
dsp=$!
(cd "$HERE/host" && exec build/fifo_relay_nre73_claude "$TX" "$RX" 5) >"$SESSION/relay.log" 2>&1 &
rel=$!
PYTHONPATH="/home/xilinx/helix_py3gpp:$STAGE" "$PYTHON" -B -u "$HERE/live/claude_live_webcam_split73_fifo_producer.py" \
  --fifo "$TX" --session-dir "$SESSION" --duration "$DURATION" --workers "$WORKERS" --bitrate-kbps "$KBPS" --framerate "$FPS" >"$SESSION/producer.log" 2>&1 &
prd=$!
wait $prd; p=$?; wait $rel; r=$?; wait $rcv; c=$?; sleep 1; kill $dsp 2>/dev/null; wait $dsp 2>/dev/null
echo "LIVE_PROCESS_RESULTS producer=$p relay=$r receiver=$c"
tail -n 1 "$SESSION/relay.log"; grep -E 'SUMMARY' "$SESSION/receiver.log" | cut -c1-300
sha256sum "$SESSION/fsoc_no_live_source.h264" "$SESSION/fsoc_no_live_display_recovered.h264" 2>&1 | cut -c1-16,65-
if cmp -s "$SESSION/fsoc_no_live_source.h264" "$SESSION/fsoc_no_live_display_recovered.h264"; then echo LIVE_SOURCE_RECOVERED_EXACT_PASS; else echo LIVE_SOURCE_RECOVERED_EXACT_FAIL; fi
