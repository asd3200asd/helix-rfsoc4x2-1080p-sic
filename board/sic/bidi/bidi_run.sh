#!/usr/bin/env bash
# Claude 2026-11-13: one board of the two-board full-duplex video test.
# usage: bash sic/bidi/bidi_run.sh <run> <node 0|1> <freq_MHz> <camera_s>        then, on BOTH boards: touch sic/bidi/<run>/go
#   1. reload helix_nlsg_sic3.bit + services, start fdx_bidi_claude (both, filler bursts until the first video byte) reading
#      sic/bidi/<run>/in.fifo and writing the received video to <run>/received.h264; start the live viewer; print ONEWAY_READY.
#   2. when <run>/go appears: orchestrate2.py calibrates the SIC on the filler (CAL s after go) and switches it on 0.1 s later,
#      the camera (1080p30 H.264, -c copy, camera_s seconds) starts CAMDELAY s after go -> source.h264 + in.fifo.
#      fdx_bidi_claude is stopped 15 s after the camera ends.
# 2026-11-13 (evening): the run directory lives in RAM (/dev/shm/bidi/<run>; sic/bidi/<run> is a symlink) and is copied to the SD card by
#   persist.sh PERSIST_DELAY (90) s after the program ends, or at the start of the next run.  SD-card write stalls had blocked the
#   control service (log line per command) and the receive thread (packet log) for 1.6-3.8 s: TX trigger time-outs and receive gaps.
# Environment (defaults = settings validated on 2026-11-13):
#   IFG=250000 tx_ifg | HOLDOFF=58980 slot hold-off in 4.07 ns beats (240 us; the bit's built-in value is 196 us) | REPEAT=6 copies
#   per TB, spread over consecutive bursts | AGC=1200 receive AGC Vref while the SIC is on (or off; 800 is the library default) | CAL=0.3 | CAMDELAY=2
#   RXN=1000000 packets per receive trigger (large, so a control-path hiccup does not stop reception)
#   VIEW=1 browser view http://<board>:18750/ | VIEW_DP=1 also full screen on the board's DisplayPort monitor | MODE=rx|tx (diagnostics)
set -u
N=$1; NODE=$2; F=$3; SEC=$4
IFG=${IFG:-250000}; HOLDOFF=${HOLDOFF:-58980}; REPEAT=${REPEAT:-6}; AGC=${AGC:-1200}; CAL=${CAL:-0.3}; CAMDELAY=${CAMDELAY:-2}
W=/home/xilinx/claude_nlsg_20261110; D=$W/sic/bidi/$N; R=/dev/shm/bidi/$N; PY=/usr/local/share/pynq-venv/bin/python3
cd $W || exit 1
# 2026-11-13: with VIEW_DP=1 the control service polls without spinning (CTRL_BUSY, default off then) so the DisplayPort player gets a
# core (this board has only 3 online CPUs); the bridge keeps spinning (BUSY).  Without VIEW_DP both follow BUSY as before.
if [ "${VIEW_DP:-0}" = 1 ]; then CB=${CTRL_BUSY-}; else CB=${CTRL_BUSY-${BUSY-1}}; fi
# 2026-11-14: VSIZE=3840x2160 for the camera's 4K H.264 (about 11 Mbit/s; use REPEAT=3).  The board cannot decode 4K in real time,
# so the DisplayPort player then shows only the IDR frames (every 100 frames, about 4 s).
# 2026-11-14: BIT=sic4 (SSB IP 1.3, runtime PSS select + PSS0/PSS1 discrimination).  PSS=split: node 1 transmits PSS1 and its receiver
# looks for PSS0, node 0 transmits PSS0 and looks for PSS1, so neither receiver synchronises to its own transmitter (needs BIT=sic4).
# 2026-11-14: default BIT=sic5 (= sic4 + stream_manager 1.1: a TX trigger that arrives during a running burst is kept, not lost) and
# PSS=split.  BIT=sic3 PSS=none gives the 2026-11-13 behaviour.
BIT=${BIT:-sic5}; RXSEL=""; SSBF=""
if [ "${PSS:-split}" = split ]; then if [ "$NODE" = 1 ]; then SSBF=matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1_PSS1.txt; RXSEL=0; else RXSEL=1; fi; fi
VSIZE=${VSIZE:-1920x1080}; DP_SKIP=""; [ "$VSIZE" = 1920x1080 ] || DP_SKIP="-skip_frame nokey"
sudo pkill -KILL -f 'sic/bidi/[^ ]*/go'; sudo pkill -KILL -f '[f]fmpeg -nostdin'; sudo pkill -KILL -f 'sic/bidi/[v]iew_server.py'; sudo pkill -f '[f]fmpeg .*-f mpjpeg'; sudo pkill -f '[t]ail -c 0 -F .*received.h264'; sudo pkill -f '[f]fplay -hide_banner'
# 2026-11-13: up to 3 attempts.  After another bitstream (Codex R3) had been loaded, the bridge once died right after start-up with
# "DMA error ... status=0x00014019" and the host program reported "Platform is not available"; reloading the bit and restarting worked.
for attempt in 1 2 3; do
for p in $(pgrep -f '[f]dx_bidi_claude|[f]dx_sic_claude|[o]rchestrate'); do sudo kill -INT $p 2>/dev/null; done; sleep 1
for i in $(seq 1 20); do pgrep -f 'build/[f]dx_|[o]rchestrate' >/dev/null || break; sleep 0.25; done; sudo pkill -KILL -f 'build/[f]dx_|[o]rchestrate'   # an fdx still running keeps the UDP port
for p in $(pgrep -f 'python3 .*([h]elix_ctrl_service|[n]l_tap_bridge)'); do sudo kill -TERM $p 2>/dev/null; done
for i in $(seq 1 20); do pgrep -f '[h]elix_ctrl_service|[n]l_tap_bridge' >/dev/null || break; sleep 0.5; done
sudo env BOARD=RFSoC4x2 XILINX_XRT=/usr $PY -B -u stepA_load_probe_$BIT.py --load 2>&1 | grep -E 'LOADED|rror'
sudo env HELIX_CTRL_BUSY=$CB STACK_LOGDIR=/dev/shm HELIX_CTRL_TIMING=/dev/shm/ctrl_t_$N.txt HELIX_DAC_VOP=40500 HELIX_CORE=1 HELIX_BUSY=${BUSY-1} HELIX_BIT=helix_nlsg_$BIT.bit bash -c 'echo x | bash run_stack.sh "$@"' _ $((SEC + 400)) $((SEC + 395)) --skip-clocks 2>&1 | tail -1
bash sic/bidi/persist.sh; sudo rm -rf "$D" "$R"; mkdir -p "$R"; chmod 777 "$R"; ln -s "$R" "$D"; mkfifo "$D/in.fifo"; chmod 666 "$D/in.fifo"; : > "$D/received.h264"; (setsid nohup timeout $((SEC + 150)) vmstat -n -t 1 > "$D/vmstat.log" 2>&1 < /dev/null &)   # 2026-11-13 diag: CPU per second
(cd $W/host && setsid nohup bash -c "exec 3<>'$D/in.fifo'; exec env HELIX_NODE=$NODE ONEWAY_MODE=${MODE:-both} ONEWAY_RAW=1 ONEWAY_PREFILL=1 ONEWAY_RXN=${RXN:-1000000} ONEWAY_REPEAT=$REPEAT ONEWAY_PKTLOG=$D/pkt.bin ONEWAY_TXLOG=$D/tx.bin ${SSBF:+HELIX_SSB_FILE=$SSBF} HELIX_FREQ_MHZ=$F timeout -s INT 900 build/fdx_bidi_claude $IFG 32 256 < '$D/in.fifo' > '$D/received.h264' 2> '$D/fdx.log'" > /dev/null 2>&1 < /dev/null &)
for i in $(seq 1 40); do grep -qaE 'ONEWAY_READY|FATAL' "$D/fdx.log" 2>/dev/null && break; [ $i -gt 4 ] && ! pgrep -f 'build/[f]dx_bidi_claude' >/dev/null && break; sleep 0.5; done
grep -qa ONEWAY_READY "$D/fdx.log" && break
echo "START_RETRY $attempt: $(grep -a 'DMA error' /dev/shm/bridge_run.log | tail -1) | $(grep -av '^Connected' "$D/fdx.log" | head -2 | tr '\n' ' ')"; cp "$D/fdx.log" "/dev/shm/fdx_failed_${N}_$attempt.log" 2>/dev/null
done
if [ -n "$RXSEL" ]; then sudo $PY -c "import mmap,os; fd=os.open('/dev/mem',os.O_RDWR|os.O_SYNC); m=memoryview(mmap.mmap(fd,0x1000,offset=0xB0009000)).cast('I'); m[2]=$RXSEL|(${PSS_NODISC:-0}<<1); print('SSB_R2',hex(m[2]))" 2>&1 | tail -1; fi   # SSB R2: bit0 = detect PSS1, bit1 = no discrimination
if [ "${VIEW:-1}" = 1 ]; then (setsid nohup $PY sic/bidi/view_server.py "$D" 18750 $((SEC + 300)) > "$D/view.log" 2>&1 < /dev/null &); fi
if [ "${VIEW_DP:-0}" = 1 ]; then
  AUTH=$(ps -C Xorg -o args= | sed -n 's/.* -auth \([^ ]*\).*/\1/p' | head -1)
  (sudo setsid nohup bash -c "$PY $W/sic/bidi/dp_feed.py '$D/received.h264' 2>'$D/dp_feed.log' | env DISPLAY=:0 XAUTHORITY=$AUTH SDL_VIDEODRIVER=x11 SDL_RENDER_DRIVER=software timeout $((SEC + 120)) nice -n 19 ffplay -hide_banner -loglevel error -fs -an -window_title 'HELIX received' -fflags nobuffer -flags low_delay -framedrop -sync ext $DP_SKIP -threads ${DP_THREADS:-2} -skip_loop_filter all -probesize 65536 -f h264 -framerate 25 -vf format=bgra -i pipe:0" > "$D/dp.log" 2>&1 < /dev/null &)
fi
(sudo setsid nohup bash -c "while [ ! -e '$D/go' ]; do sleep 0.02; done; date +%s.%N > '$D/go_at'
  env HOLDOFF=$HOLDOFF $PY sic/bidi/orchestrate2.py '$D' '$D/fdx.log' $CAL $(awk "BEGIN{print $CAL+0.1}"):1:$AGC $((SEC + 25)) > '$D/orch.log' 2>&1 &
  sleep $CAMDELAY
  ffmpeg -nostdin -hide_banner -loglevel error -f v4l2 -input_format h264 -video_size $VSIZE -framerate 30 -i /dev/video0 -c copy -t $SEC -f h264 pipe:1 2>'$D/camera.log' | tee '$D/source.h264' > '$D/in.fifo'
  date +%s.%N > '$D/camera_end'; echo CAMERA_END; sleep 15; pkill -INT -f 'build/[f]dx_bidi_claude'
  for i in \$(seq 1 60); do pgrep -f 'build/[f]dx_bidi_claude' >/dev/null || break; sleep 1; done; sleep ${PERSIST_DELAY:-90}; echo PERSIST_START; bash $W/sic/bidi/persist.sh $N; echo WAITER_DONE" > /dev/shm/waiter_$N.log 2>&1 < /dev/null &)
grep -aE 'READY|FATAL' "$D/fdx.log" | cut -c1-70; date +%T.%N | cut -c1-12
