# HELIX RFSoC4x2 — 1080p 與數位自干擾消除

雙 RFSoC4x2 的 HELIX 專案，包含 Vivado Block Design、RTL、XDC、LDPC TX HLS、SIC 模型與測試、PYNQ 板端服務、C++ 串流程式及相機串流腳本。原始 HELIX 專案資訊見 [上游 README](docs/HELIX-upstream.md)。

## 下載與建置

從 [sic3 Release](../../releases/tag/sic3) 下載 FPGA 映像、對應的 PYNQ 硬體描述、建置 IP 封裝及 SHA-256 清單。

| 檔案組 | 用途 |
|---|---|
| helix_nlsg_sic3.bit / .hwh | PC 端板子，SIC 與內建 196 µs 接收時槽間隔 |
| helix_nlsg_sic2.bit / .hwh | 1080p 全雙工測試使用的映像，測試時另外設定 196 µs |
| helix_nlsg_s6v2.bit / .hwh | Mac 端發送板使用的映像 |
| helix-sic3-build-ip.tar.gz | 本地 SSB IP、HELIX 上游 IP、網路層 IP、LDPC TX 打包 IP |

在 repo 根目錄執行下列命令。工具版本為 Vivado 2024.1，目標器件為 xczu48dr-ffvg1517-2-e。建議使用短路徑存放 checkout。

    tar -xzf helix-sic3-build-ip.tar.gz
    vivado -mode batch -source fpga/scripts/09_build_sic3.tcl -log sic3-vivado.log -journal sic3-vivado.jou

輸出為 fpga/out/helix_nlsg_sic3.bit、fpga/out/helix_nlsg_sic3.hwh 和 fpga/build_status_sic3.txt。將同名的 .bit 與 .hwh 一起放入板端工作目錄，供 PYNQ Overlay 載入。SIC3 建置入口和 XPR 的 IP 路徑已改用 repo 相對路徑。歷史建置 Tcl 保留原工作站路徑，供追溯。

## 板端程式

- 板端環境是 RFSoC4x2 的 PYNQ 3.1 映像。安裝與網路設定見 [基礎交接](fpga/software/HANDOFF_20261111.md) 和 [板端交接](fpga/software/board_sync_20261111_s6/HANDOFF_20261111f.md)。
- 板端工作目錄為 /home/xilinx/claude_nlsg_20261110。控制服務、TAP bridge 和 run_stack.sh 以 fpga/software 的交接資料配置。將 sic/board_20261112 中的全雙工腳本放到板端 sic/fdx，dual_board.sh 與 stepA_load_probe_sic*.py 放到板端工作目錄。
- C++ 基礎程式在 fpga/software/board_sync_20261111_s6/host/src。將 fpga/software/board_sync_20261111_dualboard 下的 oneway2_split6_claude.cpp 與 sic/board_20261112 下的 fdx_sic_claude.cpp 放入 host/src/examples。先建置 HELIX 函式庫，再編譯兩個範例。在板端 host 目錄執行：

    cmake -S src -B build
    cmake --build build --target HELIX
    g++ -O3 -std=c++17 src/examples/oneway2_split6_claude.cpp src/examples/helpers.cpp build/libHELIX.a -pthread -o build/oneway2_split6_claude
    g++ -O3 -std=c++17 src/examples/fdx_sic_claude.cpp src/examples/helpers.cpp build/libHELIX.a -pthread -o build/fdx_sic_claude

- 所有發送端用 IFG=200000。SIC 須在每次 RFDC 設定完成後、同一次執行內校準。go.sh 預設載入 sic2，指定 SICBIT=sic3 可載入 sic3。
- 實驗腳本使用原測試板上的路徑與 sudo 設定。run_fdx4.sh 讀取板上的 sic/fdx/local_20000tb.bin 作為本機發射資料。原始測試 payload 與相機錄影未在這次上傳的本機來源中找到。

## 原始驗證紀錄

| 測試 | 結果 |
|---|---|
| sic3 Vivado 2024.1 實作 | WNS +0.064527 ns、WHS +0.009895 ns，TNS 與 THS 均為 0 |
| sic3 單板回歸 | 16,800 / 16,800，兩次檔案逐位元相同 |
| sic3 雙板全雙工封包測試（fd20） | PC 端同頻全功率發射且保留正常 SSB。兩段 SIC 開啟期間，遠端成功率 99.5% 和 98.6%，本機封包 0 |
| sic2 雙板 1080p 全雙工（fdv2） | Mac 相機送往 PC，PC 同時全功率發射。110 秒收到 99.935% 的位元組，SIC 開啟後約 95 秒無缺洞。採接收存檔比對 |

完整脈絡見 [SIC 測試紀錄](sic/README.md) 與 [sic3 原始建置狀態](docs/build_status_sic3.txt)。
