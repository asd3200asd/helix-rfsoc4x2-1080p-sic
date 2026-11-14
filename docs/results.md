# 結果與開發紀錄

兩片板子同時互傳攝影機視訊（板 0 1649.985 MHz、板 1 1650 MHz，天線相距約 10 cm）。除非另外註明，數字是 TB 層級：接收端正確 TB 數（`rx_good`）對發送端不重複 TB 數（`tx_unique`）。rx_self＝接收端解出自己節點編號的封包數（會被丟掉，但佔用接收時槽）。

## 現行版本 sic6

| 執行 | 設定 | 視訊 | 長度 | 板 1 收板 0 | 板 0 收板 1 | rx_self | 丟棄 burst |
|---|---|---|---|---|---|---|---|
| e27 | sic6 | 1080p | 300 s | 236869/236890 (99.991 %) | 237212/237215 (99.999 %) | 0 / 0 | 0 / 0 |
| e29 | sic6 | 1080p | 600 s | 474500/474643 (99.970 %) | 474930/474938 (99.998 %) | 0 / 93824 | 0 / 0 |
| e28 | sic6 | 4K, REPEAT=3 | 300 s | 298327/298819 (99.84 %) | 298711/298760 (99.98 %) | 0 / 0 | 0 / 0 |
| e26 | sic6 PSS=split | 1080p | 300 s | **約 190 s 後兩片板子發射端停住** | | 0 / 0 | 很多 |

## 一路改了什麼

| 版本 | 改動 | 關鍵數據 |
|---|---|---|
| sic3 | 8 tap SIC、SSB 時槽保護、主機端重送與節點過濾 | 1080p 10 分鐘約 99.99 %；4K（e16，300 s）98.6 % / 98.8 %，rx_self 89303 / 137371 |
| sic4 | SSB IP 1.3：PSS0／PSS1 執行時選擇＋區分；`PSS=split` | rx_self 0；但 4K e18 板 1 發射端 30 s 後卡死 |
| sic5 | stream_manager 1.1：發射 trigger 鎖存 | 4K e20 99.88 % / 99.98 %、e22 99.92 % / 99.98 %（split）；1080p 滿載 e24 約 240 s 兩片板子卡死（split） |
| sic6 | stream_manager 1.2：busy 旗標＋控制服務等前一個 burst 送完 | 預設（不分流）1080p、4K 都正常（上表）；split 在 1080p 滿載仍會卡死 |

### PSS 區分（SSB IP 1.3）
加入 PSS1 相關器；SSB AXI-lite R2（0xB0009008）bit0＝偵測 PSS1，bit1＝關閉區分；選中的 PSS 相關值必須大於另一個才算偵測。XSim（`fpga/sim/pss_disc`，只含 PSS0 的擷取資料）：只看 PSS1 而不區分會誤觸發 4 次，有區分 0 次；PSS0／PSS1 混合輸入時只在 PSS1 burst 後觸發。`PSS=split` 時板 1 發 `slotFR2_CH1_SSB_TX1_PSS1.txt`（只有 PSS 的 64 個值不同）。

### 發射 trigger（stream_manager 1.1／1.2）
原版 `packet_fragmentation.v` 只在 `WAIT_TRIGGER` 狀態取 trigger 上升緣，而且 `transfer_size` 是即時讀的：下一個 `triggerTX` 若在前一個 burst 還在送時到達，就被漏掉或改掉正在送的長度，資料留在 TX FIFO，背壓堵住和控制共用的網路輸入，只能重載 bit。XSim（`fpga/sim/stream_manager`）重現：原版 20 個 beat 只送出 12 個、8 個卡住；1.1 版 20/20。

1.1 只有一個 pending 位置；1080p 滿載時發射端約 90 % 時間在送，落後超過一個 burst 時 trigger 仍會遺失（e24）。1.2 在 reg0 bit2 回報 busy（正在送或有 pending），`helix_ctrl_service.py` 收到 `triggerTX` 時先等 busy 清掉（最多 `HELIX_TX_WAIT_S`＝0.5 s）才回 ACK 並寫 trigger；主機收到 ACK 才送資料，所以 trigger 與資料不會錯開。實測 busy 約佔 90 % 時間。

### 還沒解決：PSS=split 在 1080p 滿載卡死
e24（sic5）與 e26（sic6）在 190～240 s 時一片板子的 trigger ACK 不再回來，數秒內另一片也停住；此時 FPGA busy 一直是 1，往主機的 ACK 與接收資料一起停止。同樣的 sic6 不分流跑 1080p／4K 都正常（e27、e28、e29），4K split 也正常（e20、e22），所以預設 `PSS=none`。

## 建置
- sic6：WNS +0.015 ns、WHS +0.006 ns（Vivado 2024.1，Performance_ExplorePostRoutePhysOpt）。