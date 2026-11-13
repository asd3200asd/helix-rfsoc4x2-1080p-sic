#!/usr/bin/env bash
# PC 端板子：fdx 程式結束後（約 175 秒）比對結果。先把 Mac 端的 live4k/sess_<名稱>/source.h264 複製成 sic/fdx/<名稱>/source.h264。
# usage: bash sic/fdx/repro_fdv_check.sh <名稱>
N=${1:?名稱}
cd /home/xilinx/claude_nlsg_20261110/sic/fdx || exit 1
pgrep -f '[f]dx_sic_claude' >/dev/null && { echo "fdx_sic_claude 還在跑，請等它結束"; exit 1; }
grep -a ONEWAY_RESULT "$N/fdx.log" | cut -c1-300
python3 filecmp.py "$N/source.h264" "$N/received.bin" | head -1
/usr/local/share/pynq-venv/bin/python3 tl.py "$N" 0 175 10
