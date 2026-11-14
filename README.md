# HELIX RFSoC4x2 — 雙向同頻全雙工 1080p／4K 視訊＋數位自干擾消除（SIC）

兩片 RFSoC4x2 在**同一頻率、同一時間**互相傳送攝影機 H.264 視訊（1080p 或 4K），兩片板子都用 SIC 消掉自己發射的訊號，只收對方的；收到的視訊可以在瀏覽器或板子的 DisplayPort 即時觀看。基於 [HELIX](docs/HELIX-upstream.md)（split 6、N_RE 73、QPSK、r=490/1024）。

目前版本：**sic6**（FPGA 映像在 [sic6 Release](../../releases/tag/sic6)）。

## 結果（2026-11-15，sic6，兩片板子同時雙向）

| 執行 | 視訊 | 長度 | 板 1 收板 0 | 板 0 收板 1 | 丟棄 burst |
|---|---|---|---|---|---|
| e27 | 1920×1080 | 300 s | 236869/236890（**99.991 %**） | 237212/237215（**99.999 %**） | 0 / 0 |
| e29 | 1920×1080 | 600 s | 474500/474643（**99.970 %**） | 474930/474938（**99.998 %**） | 0 / 0 |
| e28 | 3840×2160 | 300 s | 298327/298819（**99.84 %**） | 298711/298760（**99.98 %**） | 0 / 0 |

數字是 TB（1337 位元組的視訊區塊）層級：接收端收到的正確 TB 數對發送端送出的不重複 TB 數。e28／e29 期間板 0 在 DisplayPort 全螢幕播放、板 1 的畫面在 Mac 的 Safari 即時播放。開發過程與每一步的數據見 [docs/results.md](docs/results.md)。

## 怎麼運作

- 兩片板子都載入 `helix_nlsg_sic6.bit`：
  - 接收端在 RF-ADC 之後、AGC 之前有 8 tap 複數 FIR SIC（`fpga/src/rtl/sic/`）。
  - SSB 同步區塊（SSB IP 1.3）：時槽間隔保護（內建 196 µs，執行時改成 240 µs）；可在執行時選擇偵測 PSS0 或 PSS1，選中的 PSS 相關值必須大於另一個才算偵測。
  - stream_manager 1.2：發射 burst 還在送時到達的 `triggerTX` 會連同長度記下來接著送（以前會被漏掉，資料留在 FIFO，背壓把和控制共用的網路輸入堵住，整片板子的發射端停住）；並在 reg0 bit2 回報「還在送」，`board/helix_ctrl_service.py` 會等前一個 burst 送完才放行下一個 trigger。
- `board/host/src/examples/fdx_bidi_claude.cpp`（每片板子同時發自己的攝影機、收對方的）：
  - TB 長度欄位帶節點編號，收到自己編號的 TB 一律丟掉。
  - 每個 TB 最多送 6 次（4K 用 3 次），分散在連續的 burst；攝影機碼率高時自動減少次數（最少 2 次）。
  - 收到第一個視訊位元組之前先發填充 burst，讓 SIC 在資料送出前就校準好並開啟。
  - 每次接收 trigger 收 1,000,000 包；0.5 s 沒封包時重新 trigger。
- `board/sic/bidi/bidi_run.sh`：重載 bit、起服務（執行目錄放在 RAM `/dev/shm`，結束後才寫回 SD 卡）、啟動 `fdx_bidi_claude` 與即時檢視；收到 `go` 後由 `orchestrate2.py` 在填充訊號上校準 SIC 並開啟，2 秒後開攝影機。
- 即時檢視：`view_server.py`＋`view.html` 把收到的位元組串流給瀏覽器解碼（WebCodecs）；`VIEW_DP=1` 時板子用 ffplay 在 DisplayPort 全螢幕播放（`dp_feed.py` 跟著最新的畫面，4K 時只播關鍵幀）。
- 頻率：板 0 1649.985 MHz、板 1 1650 MHz（同標稱頻率時兩板載波差約 16～17 kHz；這樣設定後兩個方向都在約 2 kHz 內）。

## 需要的東西

- 兩片 RFSoC4x2，PYNQ 3.1 映像，`xilinx` 帳號可免密碼 sudo；各接一支 UVC 攝影機（`/dev/video0`，H.264 1920×1080；4K 要能出 3840×2160）與 ffmpeg／ffplay。
- 每片板子的 DAC B、ADC B 各接一支天線。
- FPGA 映像：[sic6 Release](../../releases/tag/sic6) 的 `helix_nlsg_sic6.bit`／`.hwh`（兩片板子用同一個）。
- 瀏覽器即時觀看：支援 WebCodecs 的瀏覽器。實測這支攝影機的 H.264 在 Safari 18.6 正常，Chrome 157 解碼失敗。

## 安裝（兩片板子都做）

板端工作目錄固定為 `/home/xilinx/claude_nlsg_20261110`，把 `board/` 的內容整個放進去，再放入 `helix_nlsg_sic6.bit`／`.hwh`：

    cd /home/xilinx/claude_nlsg_20261110/host
    cmake -S src -B build && cmake --build build --target HELIX
    g++ -O3 -std=c++17 src/examples/fdx_bidi_claude.cpp src/examples/helpers.cpp build/libHELIX.a -pthread -o build/fdx_bidi_claude

## 執行

從一台能 ssh 到兩片板子的電腦：

    # 1080p
    B0='ssh xilinx@<板0>' B1='ssh xilinx@<板1>' bash tools/run_pair.sh demo 300
    # 4K
    B0='ssh xilinx@<板0>' B1='ssh xilinx@<板1>' VSIZE=3840x2160 REPEAT=3 bash tools/run_pair.sh demo4k 300

`B0`／`B1` 是「在那片板子上執行 `bash -s`」的指令，要跳板就寫成 `B1='ssh user@跳板 ssh xilinx@<板1>'`。腳本會設時脈、兩邊啟動、同時 `go`、攝影機錄指定秒數、互相複製原始檔、兩邊各跑 `holes.py`。結果在每片板子的 `sic/bidi/<run>/`。

可用環境變數（預設＝上表的設定）：`VSIZE`（1920x1080）、`REPEAT`（最多 6 次；4K 用 3）、`VIEW_DP`（1＝板子 DisplayPort 全螢幕播放）、`BIT`（sic6）、`PSS`（none；`split`＝板 0 發 PSS0／找 PSS1、板 1 反過來，見已知限制）、`IFG`（250000）、`HOLDOFF`（58980 拍＝240 µs）、`AGC`（1200）、`RXN`（1000000）、`BUSY`（1）。

### 瀏覽器即時觀看

執行期間每片板子在 port 18750 提供收到的視訊。WebCodecs 需要安全來源，所以從看畫面的電腦開 ssh 通道，用 localhost 開：

    ssh -N -L 18750:127.0.0.1:18750 xilinx@<板子>
    # Safari 開 http://localhost:18750/

頁面上方顯示已播放幀數與解碼錯誤數，同樣的數字每 2 秒寫入板子的 `sic/bidi/<run>/view_stats.log`。

## 重建 FPGA 映像

Vivado 2024.1，目標 xczu48dr-ffvg1517-2-e。從 sic6 Release 下載 `helix-build-ip.tar.gz` 在 repo 根目錄解開（`fpga/ip_local`、`fpga/vendor`、`fpga/hls_ldpc_tx/prj3`），然後跑兩次（Vivado 在同一個 session 升級這兩個 IP 後會當掉，所以分兩次）：

    vivado -mode batch -source fpga/scripts/build_sic6_1_upgrade.tcl
    vivado -mode batch -source fpga/scripts/build_sic6_2_impl.tcl

輸出 `fpga/out/helix_nlsg_sic6.bit`／`.hwh`，進度在 `fpga/build_status_sic6.txt`。2026-11-15 從乾淨 clone＋Release 的 `helix-build-ip.tar.gz` 照這兩步重建過：WNS +0.0146 ns、WHS +0.0058 ns，與 Release 的 bit 相同。Vivado 2024.1 在這台機器上偶爾會 access violation 當掉（與腳本無關），重跑同一步即可。改過的 IP 原始碼另外放在 `fpga/ip_mod/`（方便看差異），XSim 測試在 `fpga/sim/`。

## 目錄

| 路徑 | 內容 |
|---|---|
| `board/` | 板端工作目錄的檔案：服務（`run_stack.sh`、`nl_tap_bridge.py`、`helix_ctrl_service.py`、`network_layer.*`）、載入 bit、SIC 控制（`sic_ctl.py`、`sic_model.py`、`sic_fit.py`）、時脈 |
| `board/sic/bidi/` | `bidi_run.sh`、`orchestrate2.py`、`holes.py`、`view_server.py`、`view.html`、`dp_feed.py`、`persist.sh` |
| `board/host/` | HELIX host 函式庫、`fdx_bidi_claude.cpp`、SSB 波形（PSS0 與 PSS1） |
| `tools/run_pair.sh` | 從控制電腦同時跑兩片板子 |
| `fpga/` | Vivado 工程、RTL、XDC、LDPC TX HLS、建置腳本；`ip_mod/` 改過的 SSB 1.3 與 stream_manager 1.2；`sim/` PSS 區分與發射 trigger 的 XSim 測試 |

## 已知限制

- `PSS=split`：1080p 滿載（REPEAT=6）時兩次測試都在 3～4 分鐘後兩片板子的發射端同時停住（e24、e26），原因還沒查清楚，所以預設不用；4K（REPEAT=3）時兩次 300 秒都正常。不分流時接收端有時會解出自己的封包（e29 板 1 rx_self 93824），會被丟掉，不影響傳輸。
- 偶爾 1 次發射 trigger 逾時，重試時恢復，不會丟 burst。
- SIC 預測消除量每次執行在約 5～20 dB 之間變動（每次重新設定 RFDC 後相位隨機，所以每次執行都重新校準）。
- 只有兩片板子的雙向；中繼（第三個接收點）還沒做。