#!/usr/bin/env bash
# Claude 2026-11-11 two-board one-way test helper (same file on both boards).
# usage: echo <sudo pw> | bash dual_board.sh clocks            # once after boot (TX board that was never set up)
#        echo <sudo pw> | bash dual_board.sh load              # load helix_nlsg_s6v2.bit
#        echo <sudo pw> | bash dual_board.sh stack <secs> [--skip-clocks]
#        bash dual_board.sh send <freq_MHz> <infile> [ENV=val ...]            # TX board, foreground
#        bash dual_board.sh recv <freq_MHz> <outfile> <timeout_s> [ENV=val ...]  # RX board, background; log dual_rx.err
cd /home/xilinx/claude_nlsg_20261110 || exit 1
PY="env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B -u"
M=$1; shift
case $M in
clocks) read -r PW
  echo "$PW" | sudo -S -p '' $PY -c "import xrfclk; xrfclk.set_ref_clks(lmk_freq=245.76, lmx_freq=491.52); print('CLOCKS_SET')" 2>&1 | tail -3 ;;
load) read -r PW
  echo "$PW" | sudo -S -p '' $PY stepA_load_probe_s6v2.py --load 2>&1 | tail -6 ;;
stack) read -r PW; T=$1; shift
  # run_stack.sh feeds sudo from /dev/null (works only with passwordless sudo), so run the whole script as root instead
  echo "$PW" | sudo -S -p '' env HELIX_DAC_VOP=${VOP:-40500} HELIX_CORE=1 HELIX_BUSY=1 HELIX_BIT=helix_nlsg_s6v2.bit bash -c 'echo x | bash run_stack.sh "$@"' _ $((T+5)) $T "$@" ;;
send) F=$1; IN=$2; shift 2
  cd host && env ONEWAY_MODE=tx HELIX_FREQ_MHZ=$F "$@" timeout -s INT ${TMO:-60} build/${BIN:-oneway_split6_claude} ${IFG:-180000} 32 256 < "$IN" 2>&1 >/dev/null | grep -E 'ONEWAY|FATAL|timeout|rror' | cut -c1-300 ;;
recv) F=$1; OUT=$2; T=$3; shift 3
  cd host && (setsid nohup env ONEWAY_MODE=rx HELIX_FREQ_MHZ=$F "$@" timeout -s INT $T build/${BIN:-oneway_split6_claude} ${IFG:-180000} 32 256 > "$OUT" 2> ../dual_rx.err < /dev/null &)
  for i in $(seq 1 30); do grep -qE 'ONEWAY_READY|FATAL|rror' ../dual_rx.err 2>/dev/null && break; sleep 1; done; head -5 ../dual_rx.err | cut -c1-300 ;;
# live video (background on both boards):
#   echo <pw> | bash dual_board.sh recvdp <freq> <session_dir> <timeout_s> [ENV=val ...]   # RX board: air -> ffplay on DisplayPort (+ received.h264); FFPIN=2,3 in env pins ffplay to those CPU cores (needed for 4K: the software decoder otherwise starves the bridge and reception stops)
#   echo <pw> | bash dual_board.sh sendcam <freq> <seconds> <session_dir> [size=1920x1080]   # (REPEAT=n in env) TX board: camera H.264 -> air (+ source.h264)
#   echo <pw> | bash dual_board.sh stopdp <session_dir>
recvdp) F=$1; S=$2; T=$3; shift 3; read -r PW
  mkdir -p "$S"; FIFO="$S/rx.fifo"; [ -p "$FIFO" ] || mkfifo "$FIFO"; XA=$(ls /tmp/serverauth.* 2>/dev/null | head -1)
  (echo "$PW" | sudo -S -p '' env DISPLAY=:0 XAUTHORITY="$XA" SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software setsid nohup nice -n 10 ${FFPIN:+taskset -c $FFPIN} ffplay -hide_banner -loglevel warning -stats -fs -an -autoexit \
    -f h264 -framerate 30 -fflags nobuffer -flags low_delay -framedrop -threads 4 -skip_loop_filter all -vf format=bgra -i "$FIFO" > "$S/ffplay.log" 2>&1 &)
  cd host && (setsid nohup env ONEWAY_MODE=rx HELIX_FREQ_MHZ=$F "$@" timeout -s INT $T build/${BIN:-oneway2_split6_claude} ${IFG:-180000} 32 256 "$S/received.h264" > "$FIFO" 2> ../dual_rx.err < /dev/null &)
  for i in $(seq 1 30); do grep -qE 'ONEWAY_READY|FATAL|rror' ../dual_rx.err 2>/dev/null && break; sleep 1; done; head -5 ../dual_rx.err | cut -c1-300 ;;
sendcam) F=$1; SEC=$2; S=$3; SIZE=${4:-1920x1080}; mkdir -p "$S"; read -r PW   # as root: /dev/video0 may not be readable by xilinx
  cd host && (echo "$PW" | sudo -S -p '' setsid nohup bash -c "ffmpeg -nostdin -hide_banner -loglevel error -f v4l2 -input_format h264 -video_size $SIZE -framerate 30 -i /dev/video0 -c copy -t $SEC -f h264 pipe:1 2>'$S/camera.log' | tee '$S/source.h264' | env ONEWAY_MODE=tx ONEWAY_REPEAT=${REPEAT:-2} HELIX_FREQ_MHZ=$F build/${BIN:-oneway_split6_claude} ${IFG:-180000} 32 256 >/dev/null 2>'$S/tx.log'" > /dev/null 2>&1 &)
  sleep 4; cat "$S/camera.log"; head -3 "$S/tx.log" | cut -c1-200 ;;
stopdp) S=$1; read -r PW; echo "$PW" | sudo -S -p '' pkill -x ffplay 2>/dev/null; tr '\r' '\n' < "$S/ffplay.log" | tail -n 2 | cut -c1-200 ;;
esac
