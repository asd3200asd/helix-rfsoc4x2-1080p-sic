#!/usr/bin/env bash
# PC 端板子（接收＋同頻全功率發射＋SIC）：復現 fdv2 全雙工 1080p 視訊。
# usage: bash sic/fdx/repro_fdv_pc.sh <名稱>      在板端工作目錄 /home/xilinx/claude_nlsg_20261110 執行
# 會：設時脈 → 重載 sic3 並起服務 → 背景啟動 fdx_sic_claude（本機每 TB 送 30 次）→ 第 20 秒校準、20.1 秒開 SIC（AGC 800）、135 秒關 SIC。
# 印出 ONEWAY_READY 之後 30 秒內在 Mac 端板子執行 repro_fdv_mac.sh。
set -u
N=${1:?名稱}
cd /home/xilinx/claude_nlsg_20261110 || exit 1
echo x | bash dual_board.sh clocks
SICBIT=sic3 SCRIPT=run_fdx4.sh CAL=20 IFG=200000 bash sic/fdx/go.sh "$N" 30 150 "20.1:1:800" 135
