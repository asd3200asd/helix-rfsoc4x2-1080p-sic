#!/usr/bin/env bash
# PC 端板子：產生全雙工測試的本機發射資料（20000 個 TB × 1337 位元組的亂數）。
# 內容本身無意義；tl.py／analyze2.py 用同一個檔案辨認本機封包，所以產生一次後不要再換。
cd /home/xilinx/claude_nlsg_20261110/sic/fdx || exit 1
[ -f local_20000tb.bin ] && { echo "local_20000tb.bin 已存在，保留不覆寫"; exit 0; }
head -c 26740000 /dev/urandom > local_20000tb.bin && ls -l local_20000tb.bin
