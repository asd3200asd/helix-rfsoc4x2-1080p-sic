# HELIX RFSoC4x2 — 雙向同頻全雙工 1080p 視訊＋數位自干擾消除（SIC）

兩片 RFSoC4x2 在**同一頻率、同一時間**互相傳送攝影機 1080p H.264 視訊，兩片板子都用 SIC 消掉自己發射的訊號，只收對方的；收到的視訊可以在瀏覽器即時觀看。基於 [HELIX](docs/HELIX-upstream.md)（split 6、N_RE 73、QPSK、r=490/1024）。

## 結果（2026-11-13，兩板天線相距約 10 cm）

| 執行 | 長度 | 板 0 收板 1 | 板 1 收板 0 | 備註 |
|---|---|---|---|---|
| p2（用本 repo 的 `tools/run_pair.sh`） | 60 s | **99.997 %** | **99.74 %** | |
| d8 | 60 s | **100.000 %**（0 缺洞） | **100.000 %**（0 缺洞） | 板 1 同時在瀏覽器即時播放 |
| d10 | 110 s | 99.87 % | 98.54 % | |
| d9 | 110 s | 99.88 % | 95.64 % | 板 1 發射端 2 次 trigger 逾時 |

百分比是收到的位元組對對方攝影機原始檔逐位元組比對（`holes.py`）；所有執行都沒有混入自己的資料。剩下的損失幾乎都是「某片板子發射 trigger 逾時」同一時刻約 1 秒的中斷（見已知限制）。每一步改了什麼與數據見 [docs/results_20261113.md](docs/results_20261113.md)。

## 怎麼運作

- 兩片板子都載入 `helix_nlsg_sic3.bit`：接收端在 RF-ADC 之後、AGC 之前有 8 tap 複數 FIR SIC（`fpga/src/rtl/sic/`），SSB 同步區塊有時槽間隔保護（內建 196 µs，執行時改成 240 µs）。
- `board/host/src/examples/fdx_bidi_claude.cpp`（每片板子同時發自己的攝影機、收對方的）：
  - TB 長度欄位帶節點編號，收到自己編號的 TB 一律丟掉。
  - 每個 TB 最多送 6 次，分散在連續的 burst；攝影機碼率高（2～6 Mbit/s 隨畫面變動）時自動減少次數（最少 2 次），避免發射端跟不上。
  - 收到第一個視訊位元組之前先發填充 burst（接收端會當壞包丟掉），讓 SIC 在任何資料送出前就校準好並開啟，開頭不再遺失。
  - 每次接收 trigger 收 1,000,000 包；0.5 s 沒封包時重新 trigger（主機端掉包時不會永久停住）；發射 trigger 重試時每次重試才鎖控制通道。
- `board/sic/bidi/bidi_run.sh`：重載 bit、起服務、啟動 `fdx_bidi_claude` 與即時檢視；收到 `go` 後由 `orchestrate2.py` 在填充訊號上校準 SIC（0.3 s）並開啟（AGC Vref 1200），2 秒後開攝影機。
- `board/sic/bidi/view_server.py`＋`view.html`：板子只把收到的位元組串流出去，**由瀏覽器解碼**（WebCodecs）。在板子上解碼 1080p 會干擾無線鏈路（實測一個方向掉到 90 %）。
- 頻率：板 0 1649.985 MHz、板 1 1650 MHz（同標稱頻率時兩板載波差約 16～17 kHz；這樣設定後推算兩個方向都在約 2 kHz 內）。

## 需要的東西

- 兩片 RFSoC4x2，PYNQ 3.1 映像，`xilinx` 帳號可免密碼 sudo；各接一支輸出 H.264 1920×1080 30 fps 的 UVC 攝影機（`/dev/video0`）與 ffmpeg。
- 每片板子的 DAC B、ADC B 各接一支天線。
- FPGA 映像：[sic3 Release](../../releases/tag/sic3) 的 `helix_nlsg_sic3.bit`／`.hwh`（兩片板子用同一個）。
- 即時觀看：支援 WebCodecs 的瀏覽器（Chrome、Edge）。

## 安裝（兩片板子都做）

板端工作目錄固定為 `/home/xilinx/claude_nlsg_20261110`，把 `board/` 的內容整個放進去，再放入 bit／hwh：

    cd /home/xilinx/claude_nlsg_20261110/host
    cmake -S src -B build && cmake --build build --target HELIX
    g++ -O3 -std=c++17 src/examples/fdx_bidi_claude.cpp src/examples/helpers.cpp build/libHELIX.a -pthread -o build/fdx_bidi_claude

## 執行

從一台能 ssh 到兩片板子的電腦：

    B0='ssh xilinx@<板0>' B1='ssh xilinx@<板1>' bash tools/run_pair.sh demo 60

`B0`／`B1` 是「在那片板子上執行 `bash -s`」的指令，要跳板就寫成 `B1='ssh user@跳板 ssh xilinx@<板1>'`。腳本會設時脈、兩邊啟動、同時 `go`、攝影機 60 秒、互相複製原始檔、兩邊各跑 `holes.py`。結果在每片板子的 `sic/bidi/<run>/`。

可用環境變數覆寫（預設＝上面驗證過的設定）：`IFG`（250000）、`HOLDOFF`（58980 拍＝240 µs）、`REPEAT`（最多 6 次）、`AGC`（1200）、`RXN`（1000000）、`BUSY`（1＝服務忙等）。

### 即時觀看

執行期間每片板子在 port 18750 提供收到的視訊。WebCodecs 需要安全來源，所以從看畫面的電腦開 ssh 通道，用 localhost 開：

    ssh -N -L 18750:127.0.0.1:18750 xilinx@<板子>
    # 瀏覽器開 http://localhost:18750/

頁面上方顯示已播放幀數與解碼錯誤數，同樣的數字每 2 秒寫入板子的 `sic/bidi/<run>/view_stats.log`。

## 重建 FPGA 映像

Vivado 2024.1，目標 xczu48dr-ffvg1517-2-e。從 sic3 Release 下載 `helix-sic3-build-ip.tar.gz` 在 repo 根目錄解開，然後：

    vivado -mode batch -source fpga/scripts/09_build_sic3.tcl

輸出 `fpga/out/helix_nlsg_sic3.bit`／`.hwh`。原始建置紀錄 [docs/build_status_sic3.txt](docs/build_status_sic3.txt)（WNS +0.065 ns）。

## 目錄

| 路徑 | 內容 |
|---|---|
| `board/` | 板端工作目錄的檔案：服務（`run_stack.sh`、`nl_tap_bridge.py`、`helix_ctrl_service.py`、`network_layer.*`）、載入 bit、SIC 控制（`sic_ctl.py`、`sic_model.py`、`sic_fit.py`）、時脈 |
| `board/sic/bidi/` | `bidi_run.sh`、`orchestrate2.py`、`holes.py`、`view_server.py`、`view.html` |
| `board/host/` | HELIX host 函式庫、`fdx_bidi_claude.cpp`、SSB 波形 |
| `tools/run_pair.sh` | 從控制電腦同時跑兩片板子 |
| `fpga/` | sic3 的 Vivado 工程、RTL、XDC、LDPC TX HLS、建置腳本 |

## 已知限制

- 大約每 1～2 分鐘，某片板子會有一次發射 trigger 逾時；同一時刻兩個方向都可能中斷約 1 秒（110 秒的 d9 板 1 少了約 4 %）。原因在板子的控制／資料通道（服務與橋接程式），還沒查清楚。
- SIC 預測消除量每次執行在約 5～20 dB 之間變動（每次重新設定 RFDC 後相位隨機，所以每次執行都重新校準）。
