#!/usr/bin/env bash
# Claude 2026-11-11: 4K webcam (camera-native H.264 3840x2160@30, no encoding on the board) -> HELIX burst link
#   (stream_relay_split6_claude: TAP -> SG DMA -> FPGA LDPC encoder -> DAC B -> ADC B -> PHY -> SG DMA) -> ffplay on DisplayPort.
# Prereq: helix_nlsg_s6v2.bit loaded, run_stack.sh running (HELIX_CORE=1 HELIX_BUSY=1, --skip-clocks) for longer than <seconds>.
# usage: echo <sudo-pw> | bash run_live_4k_dp.sh <session_dir> <seconds> [record=1] [size=3840x2160]
set -u
S="$1"; SEC="$2"; REC="${3:-1}"; SIZE="${4:-3840x2160}"; FPS="${5:-30}"; HERE=/home/xilinx/claude_nlsg_20261110
read -r PW
mkdir -p "$S"; F="$S/rx.fifo"; rm -f "$F"; mkfifo "$F"
XA=$(ls /tmp/serverauth.* 2>/dev/null | head -1)
echo "$PW" | sudo -S -p '' env DISPLAY=:0 XAUTHORITY="$XA" SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software ffplay -hide_banner -loglevel warning -stats -fs -an -autoexit \
  -f h264 -framerate 30 -fflags nobuffer -flags low_delay -framedrop -threads 4 -skip_loop_filter all -vf format=bgra -i "$F" >"$S/ffplay.log" 2>&1 &
RECARG=""; [ "$REC" = 1 ] && RECARG="$S/received.h264"
cd "$HERE/host"
if [ "$REC" = 1 ]; then TEE="tee $S/source.h264"; else TEE="cat"; fi
ffmpeg -hide_banner -loglevel error -f v4l2 -input_format h264 -video_size "$SIZE" -framerate "$FPS" -i /dev/video0 -c copy -t "$SEC" -f h264 pipe:1 2>"$S/camera.log" \
  | $TEE | build/stream_relay_split6_claude 180000 32 256 $RECARG >"$F" 2>"$S/relay.log"
echo "RELAY_EXIT=$?"
grep -E 'STREAM_RESULT|BURST|fail' "$S/relay.log" | tail -n 3
if [ "$REC" = 1 ]; then ls -l "$S/source.h264" "$S/received.h264" | awk '{print $5, $9}'; cmp -s "$S/source.h264" "$S/received.h264" && echo SOURCE_RECEIVED_EXACT_PASS || echo SOURCE_RECEIVED_EXACT_FAIL; fi
sleep 3; echo "$PW" | sudo -S -p '' pkill -x ffplay 2>/dev/null; tr '\r' '\n' <"$S/ffplay.log" | tail -n 2 | cut -c1-200
