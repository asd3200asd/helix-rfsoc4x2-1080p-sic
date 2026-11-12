#!/usr/bin/env bash
# Claude 2026-11-11 (TX SPLIT 6, needs helix_nlsg_s6.bit): webcam -> H.264 -> HLVX transport blocks (no LDPC in software) -> FIFO -> original HELIX host library
#   over UDP (TAP -> SG DMA -> NetworkLayer -> Stream Manager -> PHY -> DAC B -> ADC B -> PHY -> UDP) -> Split-6 FIFO
#   -> Codex receiver (HLVX CRC/FEC -> H.264 -> MJPEG preview http://192.168.3.1:<port>/).
# Prereq: bridge + control service running (run_stack.sh).  Run as normal user:
#   bash run_live_webcam_udp.sh <session_dir> <seconds> [bitrate_kbps=600] [fps=10] [workers=3] [http_port=8091] [keep_serving=2]
# Outputs in session dir: producer.log relay.log receiver.log, source/recovered .h264, summaries.
set -u
SESSION="$1"; DURATION="$2"; KBPS="${3:-600}"; FPS="${4:-10}"; WORKERS="${5:-3}"; PORT="${6:-8091}"; KEEP="${7:-2}"
STAGE=/home/xilinx/helix_webcam_capture/py3gpp_bench_stage; HERE=/home/xilinx/claude_nlsg_20261110
PYTHON=/usr/local/share/pynq-venv/bin/python3
mkdir -p "$SESSION"; TX="$SESSION/tx_tb.fifo"; RX="$SESSION/rx_split6.fifo"; rm -f "$TX" "$RX"; mkfifo "$TX" "$RX"
PYTHONPATH="/home/xilinx/helix_py3gpp:$STAGE" "$PYTHON" -B -u "$HERE/live/codex_fec_receiver_20261014.py" --board-label nlsg_udp \
  --split6-fifo "$RX" --session-dir "$SESSION" --http-port "$PORT" --keep-serving-seconds "$KEEP" >"$SESSION/receiver.log" 2>&1 &
rcv=$!
(cd "$HERE/host" && exec build/fifo_relay_split6_claude "$TX" "$RX" 5) >"$SESSION/relay.log" 2>&1 &
rel=$!
PYTHONPATH="/home/xilinx/helix_py3gpp:$STAGE" "$PYTHON" -B -u "$HERE/live/claude_live_webcam_split6_fifo_producer.py" \
  --fifo "$TX" --session-dir "$SESSION" --duration "$DURATION" --workers "$WORKERS" --bitrate-kbps "$KBPS" --framerate "$FPS" >"$SESSION/producer.log" 2>&1 &
prd=$!
wait $prd; p=$?; wait $rel; r=$?; wait $rcv; c=$?
echo "LIVE_PROCESS_RESULTS producer=$p relay=$r receiver=$c"
tail -n 1 "$SESSION/relay.log"; grep -E 'LIVE_(SOURCE_PAYLOAD_MBPS|ENCODED_VARIANT_COUNT)|PASS' "$SESSION/producer.log" | tail -n 3
sha256sum "$SESSION/fsoc_no_live_source.h264" "$SESSION/fsoc_no_live_display_recovered.h264" 2>&1 | cut -c1-16,65-
if cmp -s "$SESSION/fsoc_no_live_source.h264" "$SESSION/fsoc_no_live_display_recovered.h264"; then echo LIVE_SOURCE_RECOVERED_EXACT_PASS; else echo LIVE_SOURCE_RECOVERED_EXACT_FAIL; fi
