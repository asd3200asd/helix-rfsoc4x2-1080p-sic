#!/usr/bin/env bash
# Mac 端板子（遠端訊號源，只發射，PL 必須是 helix_nlsg_s6v2.bit，不是 sic3）：攝影機 1080p H.264 → 空中，每個 TB 送 3 次。
# usage: bash repro_fdv_mac.sh setup            # 開機後或換過 bit 之後做一次：時脈、載入 s6v2、起服務 900 秒
#        bash repro_fdv_mac.sh send <名稱>      # PC 端印出 ONEWAY_READY 後執行；送 110 秒，原始檔存在 live4k/sess_<名稱>/source.h264
set -u
cd /home/xilinx/claude_nlsg_20261110 || exit 1
case ${1:-} in
setup) echo x | bash dual_board.sh clocks
       echo x | bash dual_board.sh load
       echo x | bash dual_board.sh stack 900 --skip-clocks 2>&1 | tail -2 ;;
send)  N=${2:?名稱}
       echo x | IFG=200000 REPEAT=3 BIN=oneway2_split6_claude bash dual_board.sh sendcam 1650 110 "$PWD/live4k/sess_$N" ;;
*) echo "usage: $0 setup | send <名稱>"; exit 2 ;;
esac
