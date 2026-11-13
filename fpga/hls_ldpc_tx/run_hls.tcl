# Claude 2026-11-11: HELIX TX split-6 front end (LDPC encoder) -- Vitis HLS 2024.1
# Run (from the REAL path, not the N: subst -- csim's make fails on a subst drive):
#   cd <project>\work\nlsg\hls_ldpc_tx ; C:\Xilinx\Vitis_HLS\2024.1\bin\vitis_hls.bat -f run_hls.tcl
# Output: N:/hls_ldpc_tx/prj2/sol1/syn/report/*.rpt and IP in N:/hls_ldpc_tx/prj2/sol1/impl/ip
# Next: add the IP repo to the nlsg Vivado project and insert the block between tx_buffer_top and the TX switch S00.
open_project -reset prj2
add_files src/ldpc_tx_enc.cpp
add_files -tb tb/tb_ldpc_tx_enc.cpp -cflags "-Isrc"
add_files -tb tb/tb_blocks.bin
add_files -tb tb/ref_split73.bin
set_top ldpc_tx_enc
open_solution -reset sol1 -flow_target vivado
set_part xczu48dr-ffvg1517-2-e
create_clock -period 4.069 -name default
set_clock_uncertainty 0.6
# csim/cosim are skipped on this PC: the csim build hangs here (see hls_run notes); the same source was checked
# bit-exact (535/535 + bypass) with g++ on the board using tb/tb_ldpc_tx_enc.cpp and the shim headers.
csynth_design
export_design -format ip_catalog -vendor claude -library helix -version 1.0 -description "HELIX TX split-6 front end: CRC/segmentation/NR LDPC BG1 encode/rate match"
exit
