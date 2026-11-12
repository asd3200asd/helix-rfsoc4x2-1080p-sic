# sic_clean — 乾淨重寫的 DDC 後複數 FIR SIC（2026-11-11，Claude「SIC」對話）

只有獨立模擬與 OOC 合成；**尚未接進 nlsg BD、尚未上板**。沒有使用 v107 SIC 的任何原始碼。

## 內容
- `rtl/sic_cfir2x.sv`：每拍 2 個複數樣本、無握手、固定延遲（clog2(TAPS)+5 拍）。係數 signed 18 bit Q15，shadow 寫入後 `cfg_commit` 一拍內整組切換；`cfg_enable=0` 時輸出＝輸入延遲同樣拍數。
- `rtl/sic_ref_align.sv`：TX 參考擷取（dac_clk 一級、adc_clk 一級，不用 async FIFO）＋整數樣本延遲（BRAM）。dac_tdata → 參考的總延遲＝`cfg_delay + 10` 樣本。
- `rtl/sic_monitor.sv`：每 2^WLOG 拍鎖存 SIC 前／後功率與飽和拍數。
- `rtl/sic_top.sv`：以上三者。ADC 側 `adc_i={I1,I0}`、`adc_q={Q1,Q0}`；DAC 側假設 `{Q1,I1,Q0,I0}`（**整合前要對 TxUpsampleIFS 輸出確認**）。
- `model/sic_model.py`：逐位元黃金模型；`model/sic_fit.py`：最小平方估延遲與係數（之後板上擷取資料也用它）；`model/gen_cases.py`、`model/check_cases.py`。
- `sim/run_xsim.tcl`：在 Vivado Tcl 裡 `cd sim; source run_xsim.tcl`，再跑 `python3 model/check_cases.py`。
- `synth/ooc_synth.tcl`：sic_top 單獨 OOC 合成。

## 結果
- xsim 11 個案例全部與黃金模型逐值相同（`sim/check_result.txt`）：bypass、零係數、SI-only 殘差為 0、SI＋獨立遠端訊號輸出等於遠端訊號、滿刻度飽和（TAPS 1/4/8/16、延遲 0～4093、奇偶延遲）、流動中 5 次係數切換＋enable 切換、LS 擬合案例；monitor 每個視窗也核對。
- OOC 合成（xczu48dr-2，4.069 ns）：TAPS=4 用 52 DSP、4 BRAM、WNS +1.253 ns；TAPS=8 用 100 DSP、WNS +1.253 ns。合成估計值，未佈線；最差路徑在 monitor 的平方加總。
- `cases/summary.json` 的 fit_sweep 是**人造雙路徑通道**（非實測）：2/4/8/16 tap 的 heldout 消除量 14.4／32.4／60.3／60.4 dB。只用來驗證工具鏈，不代表板上效果。

## nlsg 接點（唯讀盤點 `design_1.bd`）
- RX：`usp_rf_data_converter_0/m20_axis`（I）、`m21_axis`（Q）→ `rx_blocks/s00_axis`、`s01_axis` → `automatic_gain_contr_0`。各 32 bit，adc_clk。SIC 插在 RFDC 與 rx_blocks 之間（AGC 之前）。
- TX 參考：`tx_blocks/m00_axis`（TxUpsampleIFS 輸出，64 bit）→ `usp_rf_data_converter_0/s00_axis`，dac_clk。
- adc_clk、dac_clk 來自同一顆 MMCM（`MTS_Block/clk_wiz_0` out2／out1），同為 245.76 MHz、相位相同。

## 還沒做
AXI-Lite 包裝；BD 整合與建置；板上 bypass 回歸（G1）；延遲校準與實測係數；遠端訊號保留（G3/G4）。

## 2026-11-11 17:15 之後：接進 nlsg
- 新增 `rtl/sic_axil.sv`（AXI-Lite 暫存器＋4096 筆成對擷取，暫存器表在檔頭）、`rtl/sic_axil_bd.v`（BD module reference 外殼）、`tb/tb_sic_axil.sv`（資料路徑逐拍比對 sic_top，暫存器／擷取／monitor 讀回；xsim PASS，24288 拍 0 錯）。
- BD：`nlsg/scripts/06_add_sic.tcl`（備份 `design_1.bd.bak_pre_sic`）＋`07_build_sic1.tcl`。`sic_0`（TAPS=8）在 RFDC m20/m21 與 rx_blocks 之間，AXI-Lite `0xB0013000`；重置後 SIC 關閉＝bypass（多 8 拍延遲）。
- 注意：用 net 取 `TxUpsampleIFS m00_axis_tdata` 會讓它脫離 interface 連線（BD 41-1306），所以 07 腳本另外把同一條 net 接到 `usp_rf_data_converter_0/s00_axis_tdata`；已在產生的 `design_1.vhd` 確認 RF-DAC tdata 有接上。
- `software/sic_ctl.py`：板上暫存器存取（status／enable／delay／coef／monitor／capture）。**尚未在板上跑過。**

## 2026-11-11 18:15～18:25 上板結果（同軸 DAC B→ADC B，400 MHz，VOP 預設，N_RE=73 QPSK，split 6；板上資料副本 `board_20261111/sic/`）
- bit `helix_nlsg_sic1.bit`（WNS +0.032）。SIC 關閉：soak 535/535、連送 4096×4＝16384/16384、59.18 Mbps，與同場 s6v2 基準相同 → **G1 通過**。
- 成對擷取（TX 參考／RX）：閒置時參考恰為 0、RX rms≈9；發射時參考 rms≈3800、RX rms≈102（此接法 ADC 端訊雜比約 21 dB）。迴路延遲 124～125 樣本（另加對齊器固定 10）。
- 同一次連送內「擷取→最小平方→寫入→開啟」：預測 20.76 dB，**硬體實測 20.86 dB**（RX 功率 8922.7 → 73.1，約等於閒置雜訊 80.7），對齊後殘餘延遲 2＝設定值。離線：1/2/4/8 tap heldout 8.9／19.3／21.4／21.4 dB（4 tap 已到雜訊底）。
- **重要限制**：每次啟動 host 程式（重新設定 RFDC mixer）後，迴路相位是隨機的（實測 −154°、110°、138°、−96°、53°…），延遲也會差 1 個樣本。同一次執行內相位穩定。所以係數必須在無線電設定完成後、同一次執行內校準；跨執行沿用舊係數無效（實測開啟後封包照樣解對＝沒有消除）。
- SIC 開啟時自發自收的封包解不出來（預期：自己的訊號被消掉），連送會停住；關閉 SIC 後不必重載即恢復（soak 100/100、連送 16384/16384）。
- `software/sic_ctl.py` 已修：係數寫入的錯誤、改為單次 32-bit 匯流排存取（原本一次 commit 可能被計多次）。
- 未做：遠端訊號保留（G3/G4）、較高發射功率／天線下的消除量（目前上限受 21 dB 訊雜比限制）、讓 NCO 相位可重現（mixer 同步更新）或把校準整合進 host 啟動流程。

## 2026-11-12 11:55～12:05 全雙工測試準備（PC 端板子，天線，1650 MHz，VOP 40500；副本 `board_20261112/`）
- 新程式 `host/src/examples/fdx_sic_claude.cpp`（oneway2 的副本：封包紀錄多存 CRC 以便分辨本機／遠端資料；任何模式下 trigger 未確認都不送資料）。`sic/fdx/run_fdx.sh`＋`orchestrate.py`（校準一次→依時間表開關 SIC）＋`analyze.py`（逐階段統計本機／遠端好包）。
- 單板自測 `solo1`（本機發射、無遠端）：SIC 關 → 自己的封包每秒 4200～5000 個全對；SIC 開 → 0 個；兩輪開關都一致，結束後 SM 正常收尾。
- **天線下的消除量要看訊號頻帶內**：全頻寬（491.52 MHz）量只有約 10 dB，因為 1800～1895 MHz（LTE B3 下行）的環境訊號落在 ADC 頻寬內、比殘差大得多。訊號頻帶內（±100 MHz）：本機干擾比環境底高 28.9 dB，**消除 28.8 dB，殘差等於閒置時的環境底**。加到 128 tap、加共軛項或三次項都沒有改善（已到底）。→ 硬體 monitor 是全頻寬功率，天線下不能直接當消除量看；之後應改在濾波後量。
- 迴路延遲（天線）123 樣本。
- 遠端訊號保留測試**尚未執行**：Mac 端板子啟動服務需要 sudo 密碼（由使用者執行）。Mac 端板子已備好 `sic/fdx/remote_50000tb.bin`（SHA `a06eca5e2eeb24e6…`，與 PC 端相同）。

## 2026-11-12 12:15～12:50 遠端訊號保留測試（兩片板子，天線，副本 `board_20261112/`）
設定：PC 端板子＝全雙工本機（`helix_nlsg_sic1.bit`，DAC B 發、ADC B 收，1649.985 MHz，VOP 40500）；Mac 端板子＝遠端（s6v2，只發射，1650 MHz，VOP 40500，每個 TB 送 8 次）。兩板距離照使用者擺法（4 支天線）。程式：本機 `fdx_sic_claude`（both 模式）＋`run_fdx.sh`／`orchestrate.py`；遠端 `oneway2_split6_claude` tx。

**結果（run `fd9`，本機發射不帶 PSS）**：本機全功率同頻發射中，遠端封包成功率
| 階段 | SIC | 遠端封包成功率 |
|---|---|---|
| 34–42 s | 開 | 97.5% |
| 42–48 s | 關 | 44.1% |
| 48–56 s | 開 | 97.7% |
| 56–62 s | 關 | 52.3% |
| 62–70 s | 開 | 97.1% |
- 對照：本機不干擾時（run `fd8`，本機發射移到 1000 MHz）跨板壞包 2.3% → SIC 開啟時已回到無干擾水準。
- 頻帶內：本機干擾 67.7 dB、遠端約 60–62 dB、環境底約 42 dB；SIC 後殘差 42.5–43.7 dB（消除 24–25 dB，到環境底）。SIC 後留下的遠端波形 CP 相關 0.94–0.98（遠端單獨 0.986）。
- 係數是在遠端發射中校準的（單次 8192 樣本擷取），仍有效；同一組係數 25 秒內頻帶內消除量維持約 28 dB（`drift1`），不需要頻繁重估。

**關鍵發現：接收鏈會被自己的同步訊號佔住**
- 本機發射帶正常 SSB 時（`fd5`～`fd7`），只要本機在頻帶內發射，遠端封包就 100% 解不出來——即使 SIC 已把本機壓到環境底（`fd6`），或本機功率降到比遠端低 14 dB（`fd7`，SIC 開或關都一樣）。SIC 後的取樣其實是乾淨的遠端波形，所以不是消除量不足。
- 本機發射移到頻帶外（`fd8`）→ 遠端解碼完全正常 → 不是 FPGA 內的數位耦合，是同頻的本機訊號本身。
- 把本機 SSB 的第 0 個符號（PSS）清零（`ssb_no_pss.txt`，`HELIX_SSB_FILE`）→ 本機接收不再偵測到自己（單板 `e1`：自己的封包 0 個），遠端保留才成立。推論：PHY 的同步／追蹤狀態會被本機（即使很弱）的 PSS 干擾。真正的中繼需要兩段鏈路用不同的同步序列，且接收端偵測器要能選擇——這是 FPGA 待辦。
- 代價：本機這次發射沒有 PSS，別的節點無法同步到它；本測試中本機發射只當干擾源。

**其他**
- 本機（全雙工那片）DAC 與 ADC 必須同頻；可用配對是本機 1649.985／遠端 1650。本機 1650／遠端 1650.015 這組 99% 壞（原因未查）。
- 遠端若「初始化後隔幾秒才開始送資料」，接收端完全收不到（原因未查）；所以改成遠端一啟動就送、本機發射延後（`LD=25`）。
- 硬體 monitor 是全頻寬功率，天線下會被 1800 MHz 以上的 LTE 蓋過；消除量一律用擷取資料離線算頻帶內。
- 每輪結束後本機接收會停在「已武裝」狀態（無 PSS 時收不到自己的收尾封包），下一輪前要重載 bit。
- 我造成的兩次 PC 端板子卡死（兩端同時初始化）已重載恢復。
- 未做：本機功率逐級掃描、長時間、即時視訊、第三節點／真正的中繼轉送。

## 2026-11-12 14:40～15:00 合併「雙板 AGC」與「SIC」兩條線（Claude「合併下一步」對話；細節見協作狀態檔 15:02 一節）
- `fd11`（本機不發射）：AGC 800 壞 2.5–4.1%；Vref 2400／3200 壞 0.01%；AGC 關 0 壞但少收約 30%。
- `fd10`（本機全功率同頻發射、不帶 PSS、SIC 開）：AGC 800 壞 4.0／5.1%；AGC 關 7.4／8.5%；Vref 3200 壞 8.5／7.8%。→ 固定增益在全雙工下沒有幫助；全雙工＋SIC 仍有 4–8% 損失（來源未查），fd9 的「回到無干擾水準」是與 AGC 基線巧合相等。
- `fd12`（本機帶 PSS、佔空約 4%）：本機一發射，遠端與本機封包全部 100% 壞且不恢復 → 本機 PSS 讓接收鏈鎖死，不是時間重疊造成的。原始碼上的候選機制：`packet_sync` 時槽中遇到第二個 PSS 偵測會歸零重來且不送 tlast（未在板上直接驗證）。
- `fd13` 無效（FEED_TB=1 會讓觸發逾時並掛掉控制路徑）。
- 新腳本：`orchestrate2.py`、`run_fdx2.sh`、`run_fdx3.sh`、`feed_sparse.py`、`analyze2.py`、`tl.py`、`pc_reload.sh`、`burstpos.py`。

## 2026-11-12 15:30～17:30 時槽間隔保護（`helix_nlsg_sic2.bit`；細節見協作狀態檔 17:30 一節）
- xsim（`work/nlsg/sim_psslock/`）：原版 packet_sync＋OFDM 解調在任何時間點插入第二個 PSS 偵測後都會自己恢復，沒有重現鎖死。鎖死改判為接收鏈的最小時槽間隔被違反（11/11 單板已量到 150 µs 會卡住）。
- 修改：`work/nlsg/ip_local/SSB_v3_5G_IP_v1_1`（packet_sync.vhd、SSB_TOP_v2_wrp.vhd）——偵測只在沒有時槽進行中且距上一個被接受的時槽起點 ≥ 間隔時才開始新時槽；預設 165 µs，SSB R1[31:17] 可覆寫。
- sic2 上板：單板回歸通過；本機帶正常 SSB 稀疏發射時，165 µs 約 7 秒後接收停止（fd14），200 µs 全程正常且 SIC 開關行為正確（fd15，遠端收到一半）。175 µs 未測（Mac 端板子失聯）。

## 2026-11-12 17:40～18:00 不拿掉 PSS 的全雙工（sic2，tx_ifg 200000，時槽間隔 196 µs）
- `fd18`：本機全功率全速率、正常 SSB，SIC 開 → 遠端 97.6–98.1%（基準 97.9%）、本機封包 0；SIC 關 → 兩邊分掉接收；165 秒無鎖死。
- `fd19`：SIC 連續開 4.6 分鐘、單次校準 → 遠端 98.0–98.5% 持平。
- 間隔 165／175 µs（tx_ifg 180000）會在 7／50 秒後讓接收停止；需 tx_ifg 200000＋196 µs。跑法見協作狀態檔 18:00 一節（`go.sh`、`remote_tx2.sh`）。

## 2026-11-12 18:10～19:12 全雙工即時視訊與 `helix_nlsg_sic3.bit`
- 全雙工即時視訊（sic2＋HOLDOFF 196 µs，Mac 攝影機 1080p→PC，PC 同時全功率發射、正常 SSB、SIC 開）：`fdv2` 每 TB 送 3 次、110 秒，收到 99.935% 位元組，SIC 開啟後約 95 秒缺洞 0；`fdv1` 送 2 次，SIC 開啟後缺 34/23021。只存檔比對（`filecmp.py`）。
- `helix_nlsg_sic3.bit`＝sic2 但內建間隔 196 µs（`ip_local/SSB_v3_5G_IP_v1_2`、`scripts/09_build_sic3.tcl`）。單板回歸通過（tx_ifg 200000）；`fd20` 不寫暫存器的全雙工：SIC 開遠端 99.5%／98.6%。規則：tx_ifg 一律 200000。
