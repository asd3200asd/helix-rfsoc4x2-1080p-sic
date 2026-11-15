# HELIX RFSoC4x2 — 雙向同頻全雙工 1080p 視訊＋數位自干擾消除（SIC）

兩片 RFSoC4x2 在**同一頻率、同一時間**互相傳送攝影機 1080p H.264 視訊，兩片板子都用 SIC 消掉自己發射的訊號，只收對方的。PC 端板子把收到的畫面放在 DisplayPort 螢幕上；Mac 端板子收到的畫面在 Mac 的 Safari 裡播放，同一頁可以開關 SIC、看消除量。基於 [HELIX](docs/HELIX-upstream.md)（split 6、N_RE 73、QPSK、r=490/1024）。

目前版本：**sic6**（FPGA 映像在 [sic6 Release](../../releases/tag/sic6)）。

## 結果（2026-11-15，1080p，PC 端與 Mac 端同時雙向）

| 執行 | 長度 | PC → Mac | Mac → PC | 丟棄 burst | PC 端 DisplayPort 追趕跳格 |
|---|---|---|---|---|---|
| e43 | 600 s | 193645/193663（**99.991 %**） | 151429/151430（**99.999 %**） | 0 / 0 | 37 |
| e42 | 600 s | 181719/181756（**99.980 %**） | 215513/215513（**100.000 %**） | 0 / 0 | 139（PC 端也跑 SIC 量測服務時） |

數字是 TB（1337 位元組的視訊區塊）層級：接收端收到的正確 TB 數對發送端送出的不重複 TB 數。

**延遲**（兩片板子的時鐘以 NTP 同步，兩個方向一起算可消去時鐘差）：攝影機寫出 → 對方板子收到的檔案 **0.15 s 以內**（e36、e43）；PC 端 DisplayPort 的餵入端一般只落後收到的檔案約 0.06 s（播放器內部另計），Mac 端 Safari 收到就解碼。

**SIC 開 vs 關**（e44，兩片板子每 12 秒一起切換；數字是 Mac 端板子收 PC 端攝影機時的即時量測，見下面的展示頁）：

| | SIC 開 | SIC 關 |
|---|---|---|
| 自干擾消除量 | **31.5～38.0 dB** | 0 dB |
| SIR（對方訊號 ÷ 自干擾） | +22.7～+28.0 dB | −4.4～−7.5 dB |
| 壞封包比例 | 3.7～4.1 % | **36.9～43.8 %** |

每一步改了什麼與數據見 [docs/results.md](docs/results.md)。

## 怎麼運作

- 兩片板子都載入 `helix_nlsg_sic6.bit`：
  - 接收端在 RF-ADC 之後、AGC 之前有 8 tap 複數 FIR SIC（`fpga/src/rtl/sic/`），每次執行在填充訊號上校準一次。
  - SSB 同步區塊（SSB IP 1.3）：時槽間隔保護；可在執行時選擇偵測 PSS0 或 PSS1，選中的 PSS 相關值必須大於另一個才算偵測。
  - stream_manager 1.2：發射 burst 還在送時到達的 `triggerTX` 會連同長度記下來接著送，並在 reg0 bit2 回報「還在送」；`board/helix_ctrl_service.py` 等前一個 burst 送完才放行下一個。
- `board/host/src/examples/fdx_bidi_claude.cpp`（每片板子同時發自己的攝影機、收對方的）：
  - TB 長度欄位帶節點編號，收到自己編號的 TB 一律丟掉。
  - 每個 TB 送 3 次，分散在連續的 burst；碼率高時自動減少（最少 2 次）。
  - 每個發射 trigger 帶序號；控制服務對同一個序號只執行一次、重複的只回 ACK，所以 ACK 遺失時主機可以安全地重送，直到確認才送資料。
  - 收到第一個視訊位元組之前先發填充 burst，讓 SIC 在資料送出前就校準好並開啟。
- `board/sic/bidi/bidi_run.sh`：重載 bit、起服務（執行目錄放在 RAM，結束後才寫回 SD 卡）、啟動 `fdx_bidi_claude` 與即時檢視；收到 `go` 後由 `orchestrate2.py` 校準 SIC 並開啟，2 秒後開攝影機。
- PC 端 DisplayPort（`VIEW_DP=1`）：`dp_feed.py` 跟著收到的檔案最新處餵給 ffplay，ffplay 以 16 位元（螢幕本身的格式）全螢幕顯示、解碼每一幀但每兩幀顯示一幀（板子沒有 GPU，軟體繪製全螢幕 1080p 最多約 20 fps）。
- Mac 端展示（`sic_api.py`＋`demo.html`＋`view_server.py`＋`view.html`，只在 Mac 端板子上跑）：板子只把收到的位元組串流給 Safari 解碼（WebCodecs）；`sic_api.py` 提供 SIC 開關與每秒一次的自干擾消除量量測（擷取 8192 個樣本，從發射參考訊號擬合出 ADC 上的自干擾，算 SIC 之後剩多少）。它在 bit 載入後才啟動、下次載入前先停掉（bit 重載時讀 PL 暫存器會讓 CPU 卡死）。
- 頻率：PC 端 1649.985 MHz、Mac 端 1650 MHz。兩片板子的晶振在同一設定下差約 16～17 kHz（接近半個子載波間距），PC 端設低 15 kHz 後兩個方向都在約 2 kHz 內；兩個訊號的頻寬完全重疊。

## 需要的東西

- 兩片 RFSoC4x2，PYNQ 3.1 映像，`xilinx` 帳號可免密碼 sudo；各接一支 UVC 攝影機（`/dev/video0`，H.264 1920×1080）與 ffmpeg／ffplay。
- 每片板子的 DAC B、ADC B 各接一支天線。
- FPGA 映像：[sic6 Release](../../releases/tag/sic6) 的 `helix_nlsg_sic6.bit`／`.hwh`（兩片板子用同一個）。
- PC 端板子接 DisplayPort 螢幕；Mac 用 Safari（實測這支攝影機的 H.264 在 Safari 18.6 正常，Chrome 157 解碼失敗）。

## 安裝（兩片板子都做）

板端工作目錄固定為 `/home/xilinx/claude_nlsg_20261110`，把 `board/` 的內容整個放進去，再放入 `helix_nlsg_sic6.bit`／`.hwh`：

    cd /home/xilinx/claude_nlsg_20261110/host
    cmake -S src -B build && cmake --build build --target HELIX
    g++ -O3 -std=c++17 src/examples/fdx_bidi_claude.cpp src/examples/helpers.cpp build/libHELIX.a -pthread -o build/fdx_bidi_claude

## 執行

從一台能 ssh 到兩片板子的電腦（PC 端板子＝節點 0，Mac 端板子＝節點 1）：

    B0='ssh xilinx@<PC 端板子>' B1='ssh xilinx@<Mac 端板子>' VIEW_DP=1 bash tools/run_pair.sh demo 600

`B0`／`B1` 是「在那片板子上執行 `bash -s`」的指令，要跳板就寫成 `B1='ssh user@跳板 ssh xilinx@<板子>'`。腳本會設時脈、兩邊啟動、同時 `go`、攝影機錄指定秒數、互相複製原始檔、兩邊各跑 `holes.py`。結果在每片板子的 `sic/bidi/<run>/`。

可用環境變數（預設＝上表的設定）：`VIEW_DP`（1＝PC 端 DisplayPort 全螢幕播放）、`REPEAT`（3）、`SIC_API`（預設只在 Mac 端板子）、`BIT`（sic6）、`IFG`（250000）、`HOLDOFF`（58980 拍＝240 µs）、`AGC`（1200）、`RXN`（1000000）、`BUSY`（1）。

### Mac 端展示頁

執行期間從 Mac 開一條 ssh 通道到 Mac 端板子，再用 Safari 開：

    ssh -N -L 18750:127.0.0.1:18750 -L 18751:127.0.0.1:18751 xilinx@<Mac 端板子>
    # Safari 開 http://localhost:18751/

頁面左邊是 PC 端攝影機經無線到 Mac 端板子、在這台 Mac 解碼的畫面；右邊是 Mac 端板子的 SIC 開／關按鈕、自動 A/B（每 15 秒切換）、自干擾消除量、SIR、收到的 TB 與壞封包、最近 3 分鐘的圖（SIC 關的時段塗紅）；下面是兩個方向同時在送的資料量。Mac → PC 的畫面在 PC 端的 DisplayPort 螢幕上。

## 重建 FPGA 映像

Vivado 2024.1，目標 xczu48dr-ffvg1517-2-e。從 sic6 Release 下載 `helix-build-ip.tar.gz` 在 repo 根目錄解開，然後跑兩次（Vivado 在同一個 session 升級 IP 後會當掉）：

    vivado -mode batch -source fpga/scripts/build_sic6_1_upgrade.tcl
    vivado -mode batch -source fpga/scripts/build_sic6_2_impl.tcl

輸出 `fpga/out/helix_nlsg_sic6.bit`／`.hwh`。從乾淨 clone 照這兩步重建過：WNS +0.0146 ns，與 Release 的 bit 相同。改過的 IP 原始碼在 `fpga/ip_mod/`，XSim 測試在 `fpga/sim/`。

## 目錄

| 路徑 | 內容 |
|---|---|
| `board/` | 板端工作目錄的檔案：服務（`run_stack.sh`、`nl_tap_bridge.py`、`helix_ctrl_service.py`、`network_layer.*`）、載入 bit、SIC 控制（`sic_ctl.py`、`sic_model.py`、`sic_fit.py`）、時脈 |
| `board/sic/bidi/` | `bidi_run.sh`、`orchestrate2.py`、`holes.py`、即時檢視（`view_server.py`、`view.html`、`dp_feed.py`）、展示（`sic_api.py`、`demo.html`）、量測（`sic_ab.py`、`lat_probe.sh`） |
| `board/host/` | HELIX host 函式庫、`fdx_bidi_claude.cpp`、SSB 波形 |
| `tools/run_pair.sh` | 從控制電腦同時跑兩片板子 |
| `fpga/` | Vivado 工程、RTL、XDC、LDPC TX HLS、建置腳本、改過的 IP、XSim 測試 |

## 已知限制

- 偶爾（約幾分鐘一次）某片板子的發射端與 FPGA 之間的網路輸入整個停住，發射端就停到重新執行為止；每個 TB 送 6 次（負載約 90 %）時最常發生，送 3 次的 10 分鐘測試都沒有發生。原因在 FPGA 網路輸入那一段，軟體看不到，還在查。
- PC 端 DisplayPort 每兩幀顯示一幀（約 12.5 fps）。
- SIC 消除量每次執行重新校準，約 18～40 dB 之間變動。
- 兩片板子的頻率差是這兩片的晶振量出來的；換板子要重量。