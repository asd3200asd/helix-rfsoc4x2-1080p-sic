## 2026-11-12 18:22（Claude「合併下一步」對話）全雙工即時視訊結果；sic3 仍在建置（請勿開啟 `work/nlsg/p`）
- **全雙工即時視訊成立**（sic2、兩板 tx_ifg 200000、間隔 196 µs；Mac 攝影機 1080p→PC，PC 同時全功率全速率發射自己的資料、帶正常 SSB、SIC 開）：
  - `fdv2`（每個 TB 送 3 次，110 秒，38.3 MB）：收到 99.935% 的位元組、沒有混入本機資料；只缺 2 段（各約 12 kB＝一整個攝影機 burst），都發生在本機剛開始發射、SIC 還沒開的那 3 秒；SIC 開啟後約 95 秒缺洞 0。
  - `fdv1`（送 2 次）：SIC 開啟後 85 秒，23,021 個 TB 缺 34 個（0.15%，皆單一 TB）；單包壞率 1.96%。開頭另有約 350 個缺洞，是我讓本機先發射了 5 秒才開 SIC（本機封包的序號與遠端撞號）造成的。
  - 這兩輪只存檔比對（`sic/fdx/fdv*/received.bin` 對 Mac 端 `live4k/sess_fdx_video*/source.h264`，用 `filecmp.py`），沒有接 ffplay 顯示。
- 跑法：PC 端 `SCRIPT=run_fdx4.sh CAL=20 IFG=200000 HOLDOFF=48170 bash sic/fdx/go.sh <名稱> 30 150 "20.1:1:800" 135` → READY 後 Mac 端 `echo x | IFG=200000 REPEAT=3 BIN=oneway2_split6_claude bash dual_board.sh sendcam 1650 110 <絕對路徑 session>`。
- 兩板的 `dual_board.sh` 已加 `IFG` 環境變數（預設仍 180000；Mac 端有備份 `dual_board.sh.bak_pre_ifg`，PC 端的尚未改）。
- sic3（內建預設 196 µs）建置 18:09 啟動，預計 19:05 前後完成；完成後要做：單板回歸、不寫暫存器重跑全雙工。**用 sic3 時所有程式的 tx_ifg 都要 ≥200000**，否則每兩個時槽只收一個。

## 2026-11-12 19:05 進行中（Claude「合併下一步」對話，**使用兩片板子**；Vivado 工程已釋出）
- `out/helix_nlsg_sic3.bit` 建置完成（19:02）：WNS +0.065 ns、TNS 0、WHS +0.0099 ns、THS 0。＝sic2 但內建間隔預設 196 µs。工作：上板驗證（單板回歸、不寫暫存器的全雙工）。

## 2026-11-12 19:12 交接（Claude「合併下一步」對話結束本輪）▶ 下一對話從這裡開始
- 目前工作者：無。Vivado 工程已釋出（BD 的 SSB＝本地 IP 1.2）。兩片板子都無程序（程式自己結束後才停服務）。**PC 端 PL＝`helix_nlsg_sic3.bit`（SIC 關閉）**；Mac 端 PL＝s6v2。
- **`helix_nlsg_sic3.bit`（WNS +0.065）驗證通過**——內建時槽間隔 196 µs，不必再寫暫存器：
  - 單板自發自收 tx_ifg 200000：16800/16800、壞 0、FILE_EXACT（兩次）。tx_ifg 180000：如預期只收到一半（8399 包），之後改回 200000 立即正常，不需重載。
  - `fd20`（全雙工、本機全功率全速率、正常 SSB、**沒有寫 HOLDOFF**）：SIC 開兩段遠端成功率 99.5%、98.6%，本機封包 0；SIC 關兩段兩邊分掉；全程無鎖死、無逾時。
- **使用 sic3（或 sic2）的規則：所有發射端 tx_ifg 用 200000**（`IFG=200000`；兩板的 `dual_board.sh`、PC 端 `sw.sh` 已加 `IFG` 變數，預設仍 180000，備份 `dual_board.sh.bak_pre_ifg`）。在 sic3 上用 180000 會每兩個時槽只收一個。要暫時回到舊行為可在 READY 後把 SSB R1[31:17] 寫成小值（例如 1）。
- `SICBIT=sic3 … bash sic/fdx/go.sh …` 會載入 sic3（`go.sh` 預設仍是 sic2）。
