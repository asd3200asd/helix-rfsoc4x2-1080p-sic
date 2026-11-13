# HELIX RFSoC4x2 — 雙向同頻全雙工 1080p 視訊＋數位自干擾消除（SIC）

兩片 RFSoC4x2 在**同一頻率、同一時間**互相傳送攝影機 1080p H.264 視訊，兩片板子都用 SIC 消掉自己發射的訊號，只收對方的。基於 [HELIX](docs/HELIX-upstream.md)（split 6、N_RE 73、QPSK、r=490/1024）。

## 結果（2026-11-13，兩板天線相距約 10 cm）

| 執行 | 長度 | 板 0 收板 1 的視訊 | 板 1 收板 0 的視訊 | 混入自己的資料 |
|---|---|---|---|---|
| p1（用本 repo 的 `tools/run_pair.sh` 執行） | 60 s | **99.77 %**（2 個缺洞） | **99.94 %**（8 個缺洞） | 0 |
| b13 | 60 s | 99.50 % | 99.91 % | 0 |
| b14 | 110 s | 99.43 %（2664／2679 幀可解） | 96.07 %（2643／2754 幀可解） | 0 |

百分比是收到的位元組對對方攝影機原始檔逐位元組比對（`holes.py`）。b14 板 1 的損失集中在少數幾個大缺洞（檔案 80～81 % 處），該次板 1 發射端也記錄到一次 trigger 逾時（兩者時間點未對照）。更多數據與每個設定的作用見 [docs/results_20261113.md](docs/results_20261113.md)。

## 怎麼運作

- 兩片板子都載入 `helix_nlsg_sic3.bit`：接收端在 RF-ADC 之後、AGC 之前有 8 tap 複數 FIR SIC（`fpga/src/rtl/sic/`），SSB 同步區塊有時槽間隔保護（內建 196 µs，執行時改成 240 µs）。
- `board/host/src/examples/fdx_bidi_claude.cpp`：每片板子同時發射自己的攝影機串流、接收對方的串流。
  - 每個 TB 的長度欄位帶節點編號（0／1），收到自己編號的 TB 一律丟掉（SIC 開啟前自己的封包會被解出來，兩邊序號又都從 0 開始）。
  - 每個 TB 送 6 次，**分散在 6 個連續 burst**，兩邊 burst 重疊時掉一整個 burst 也不會丟資料。
  - 一批接收若有封包在主機端遺失，Stream Manager 數完後不會再送；0.5 s 沒有封包就重新 trigger（否則接收會整個停住）。
- `board/sic/bidi/bidi_run.sh`：重載 bit、起服務、啟動 `fdx_bidi_claude`；收到 `go` 後同時開攝影機和 `orchestrate2.py`（攝影機開始 1 s 後擷取 TX 參考與 RX、最小平方算係數、寫入並開 SIC，AGC Vref 800）。
- 頻率：板 0 1649.985 MHz、板 1 1650 MHz（同標稱頻率時實測兩板載波差約 16～17 kHz；這樣設定後推算兩個方向都在約 2 kHz 內）。

## 需要的東西

- 兩片 RFSoC4x2，PYNQ 3.1 映像，`xilinx` 帳號可免密碼 sudo；各接一支輸出 H.264 1920×1080 30 fps 的 UVC 攝影機（`/dev/video0`）與 ffmpeg。
- 每片板子的 DAC B、ADC B 各接一支天線。
- FPGA 映像：[sic3 Release](../../releases/tag/sic3) 的 `helix_nlsg_sic3.bit`／`.hwh`（兩片板子用同一個）。

## 安裝（兩片板子都做）

板端工作目錄固定為 `/home/xilinx/claude_nlsg_20261110`，把 `board/` 的內容整個放進去，再放入 bit／hwh：

    cd /home/xilinx/claude_nlsg_20261110/host
    cmake -S src -B build && cmake --build build --target HELIX
    g++ -O3 -std=c++17 src/examples/fdx_bidi_claude.cpp src/examples/helpers.cpp build/libHELIX.a -pthread -o build/fdx_bidi_claude

## 執行

從一台能 ssh 到兩片板子的電腦：

    B0='ssh xilinx@<板0>' B1='ssh xilinx@<板1>' bash tools/run_pair.sh demo 60

`B0`／`B1` 是「在那片板子上執行 `bash -s`」的指令，要跳板就寫成 `B1='ssh user@跳板 ssh xilinx@<板1>'`。腳本會：設時脈 → 兩邊 `bidi_run.sh` → 等兩邊 `ONEWAY_READY` → 同時 `go` → 攝影機 60 秒 → 互相複製原始檔 → 兩邊各跑 `holes.py`。結果在每片板子的 `sic/bidi/<run>/`（`received.h264` 可直接用 ffplay 播）。

可用環境變數覆寫：`IFG`（250000）、`HOLDOFF`（58980 拍＝240 µs）、`REPEAT`（6）、`AGC`（800）。

## 重建 FPGA 映像

Vivado 2024.1，目標 xczu48dr-ffvg1517-2-e。從 sic3 Release 下載 `helix-sic3-build-ip.tar.gz` 在 repo 根目錄解開，然後：

    vivado -mode batch -source fpga/scripts/09_build_sic3.tcl

輸出 `fpga/out/helix_nlsg_sic3.bit`／`.hwh`。原始建置紀錄 [docs/build_status_sic3.txt](docs/build_status_sic3.txt)（WNS +0.065 ns）。

## 目錄

| 路徑 | 內容 |
|---|---|
| `board/` | 板端工作目錄的檔案：服務（`run_stack.sh`、`nl_tap_bridge.py`、`helix_ctrl_service.py`、`network_layer.*`）、載入 bit、SIC 控制（`sic_ctl.py`、`sic_model.py`、`sic_fit.py`）、時脈 |
| `board/sic/bidi/` | `bidi_run.sh`、`orchestrate2.py`、`holes.py` |
| `board/host/` | HELIX host 函式庫、`fdx_bidi_claude.cpp`、SSB 波形 |
| `tools/run_pair.sh` | 從控制電腦同時跑兩片板子 |
| `fpga/` | sic3 的 Vivado 工程、RTL、XDC、LDPC TX HLS、建置腳本 |

## 已知限制

- 板 1 收板 0（PC 端板子 → Mac 端板子）這個方向，**就算板 1 不發射**，單包壞率也約 25 %（另一方向約 5～13 %），靠 6 次重送補回；原因未查（天線方向／位置是候選）。
- SIC 預測消除量每次執行在 5～15 dB 之間變動；SIC 開啟前約 1 秒的資料兩邊都會遺失（每次的開頭缺洞）。
- 110 秒的 b14 中板 1 有一段集中的損失（約 4 %），原因未查；發射 trigger 逾時重試期間控制通道被佔住、接收無法重新 trigger 是候選之一。
- 只有存檔比對；接收端沒有即時螢幕播放的整合腳本（`received.h264` 事後可播）。
