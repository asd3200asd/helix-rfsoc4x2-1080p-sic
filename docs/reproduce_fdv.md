# 復現：雙板同頻全雙工 1080p 視訊＋SIC（fdv2）

這份文件只講怎麼用這個 repo 的檔案重跑 11/12 的 fdv2 結果。2026-11-13 已照這裡的步驟在原測試板上重跑兩次（結果在最後）。

## 這個實驗是什麼

- **PC 端板子**：PL＝`helix_nlsg_sic3.bit`。收 Mac 端送來的視訊，同時在**同一頻率**以全功率、全速率發射自己的資料（帶正常 SSB），SIC 把自己的訊號消掉。
- **Mac 端板子**：PL＝`helix_nlsg_s6v2.bit`，**只發射**（攝影機 1080p H.264，每個 TB 送 3 次），是遠端訊號源。
- 驗收：PC 收到的位元組對 Mac 端原始檔逐位元組比對（`filecmp.py`），並逐 10 秒統計本機封包是否被消掉（`tl.py`）。
- 頻率：Mac 1650 MHz、PC 1649.985 MHz（`go.sh` 內建），兩板天線相距約 10 cm。

**不是**兩邊同時互傳視訊（雙向）。雙向是另一個未完成的實驗，不要把 Mac 端換成 sic3，也不要用改過發射節奏的 `fdx_bidi`。

## 必要條件（少一項就跑不出來）

| 項目 | 值 |
|---|---|
| PC 端 PL | `helix_nlsg_sic3.bit`／`.hwh`（內建 196 µs 時槽間隔） |
| Mac 端 PL | `helix_nlsg_s6v2.bit`／`.hwh` |
| 兩邊發射間隔 | `IFG=200000`（用 sic3 時 180000 會每兩個時槽只收一個） |
| PC 端程式 | `fdx_sic_claude`，原始碼 `sic/board_20261112/fdx_sic_claude.cpp` |
| Mac 端程式 | `oneway2_split6_claude`，原始碼 `fpga/software/board_sync_20261111_dualboard/oneway2_split6_claude.cpp` |
| HELIX 函式庫 | `fpga/software/board_sync_20261111_s6/host/src`，其中 `include/helix.h` 的 `udp` 必須是 public（`fdx_sic_claude` 會用到） |
| SIC 校準 | 每次 RFDC 設定後、同一次執行內校準（`orchestrate2.py` 在第 20 秒自動做） |

## 檔案放到板端哪裡

板端工作目錄 `/home/xilinx/claude_nlsg_20261110`（兩片板子相同）：

| 放到 | 檔案（repo 路徑） |
|---|---|
| 工作目錄 | `sic/board_20261112/` 的 `dual_board.sh`、`run_stack.sh`、`helix_ctrl_service.py`、`nl_tap_bridge.py`、`stepA_load_probe_s6v2.py`、`stepA_load_probe_sic3.py`、`repro_fdv_mac.sh`；`fpga/software/network_layer.py`／`.json`；`sic/software/sic_ctl.py`；`sic/model/sic_model.py`、`sic_fit.py`；release 的 `.bit`／`.hwh` |
| `sic/fdx/` | `sic/board_20261112/` 的 `go.sh`、`pc_reload.sh`、`run_fdx4.sh`、`orchestrate2.py`、`tl.py`、`filecmp.py`、`analyze2.py`、`make_local_payload.sh`、`repro_fdv_pc.sh`、`repro_fdv_check.sh` |
| `host/` | `fpga/software/board_sync_20261111_s6/host/src` 整個放成 `host/src`；兩個範例放進 `host/src/examples`，編譯方式見 repo 根目錄 README |
| `matlab/TX_data/...` | 依 `fpga/software` 交接資料（`fdx_sic_claude` 讀 `slotFR2_CH1_SSB_TX1.txt`） |

兩板以上述檔案逐檔核對過，與 11/12 實驗時板上的版本 MD5 相同（`helix.h`、`run_stack.sh`、`helix_ctrl_service.py`、`nl_tap_bridge.py`、`fdx_sic_claude.cpp`、`oneway2_split6_claude.cpp`、`dual_board.sh`、`go.sh`、`run_fdx4.sh`、`orchestrate2.py`、`sic_ctl.py`、`sic_model.py`、`sic_fit.py`、`stepA_load_probe_*.py`、`helix_nlsg_sic3.bit`）。

## 步驟

1. PC 端（只做一次）：`bash sic/fdx/make_local_payload.sh`
2. Mac 端：`bash repro_fdv_mac.sh setup`（時脈、載入 s6v2、起服務 900 秒）
3. PC 端：`bash sic/fdx/repro_fdv_pc.sh ghfdv`，等它印出 `ONEWAY_READY`
4. 30 秒內，Mac 端：`bash repro_fdv_mac.sh send ghfdv`
5. 約 3 分鐘後（PC 端 `fdx_sic_claude` 自己結束後才可以停服務），把 Mac 端 `live4k/sess_ghfdv/source.h264` 複製到 PC 端 `sic/fdx/ghfdv/source.h264`
6. PC 端：`bash sic/fdx/repro_fdv_check.sh ghfdv`

注意：遠端要先送完、本機程式才結束；不要在本機還在發射時停服務（會讓發射端 trigger 逾時，要重載 bit）。

## 2026-11-13 復現結果（同兩片板子、同擺位）

| 執行 | Mac 原始檔 | PC 收到 | 缺洞 | 混入本機資料 | SIC 校準預測 | 備註 |
|---|---|---|---|---|---|---|
| 11/12 fdv2（原始，sic2＋寫入 196 µs） | 38,316,245 B | 99.935 % | 2 段（都在開 SIC 前） | 0 | 8.8 dB | 原始紀錄 |
| 11/13 ghfdv1（sic3） | 28,165,695 B | 98.07 % | 125 段（多為單一 TB） | 0 | 9.9 dB | 這次 ADC 峰值 815（fdv2 為 605），SIC 開啟後壞包從約 300／秒漸增到約 2400／秒 |
| 11/13 ghfdv2（sic3） | 28,336,577 B | **99.81 %** | 28 段（皆單一 TB 以內） | 0 | 5.3 dB | ADC 峰值 614；SIC 開啟期間本機封包 0、壞包約 40／秒 |

兩次 SIC 開啟期間本機封包都是 0（自己的訊號被消掉），遠端視訊沒有混入本機資料；SIC 關閉後（135 秒起）本機封包立即恢復約 4400／秒。ghfdv1 比較差，原因未查（候選：該次自干擾較強、校準擷取不同）；兩次的差距說明單次結果有變異，需要多跑幾次取統計。
