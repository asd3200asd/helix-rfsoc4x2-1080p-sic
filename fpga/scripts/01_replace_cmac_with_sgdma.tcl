# Claude 2026-11-10: replace the 100G CMAC with one pair of PS SG-DMA streams on the
# Ethernet side of the NetworkLayer (S_AXIS_eth2nl / M_AXIS_nl2eth).
# Project must be open (work/nlsg/p/nlsg.xpr). Writes a log next to this script.
set W C:/Users/asd32/Documents/Codex/2026-10-31/helix-rfsoc-4x2-qsfp-codex/work/nlsg
set LOG [open $W/scripts/01_replace_cmac_with_sgdma.log w]
proc L {m} { global LOG; puts $LOG $m; flush $LOG }
if {[catch {
open_bd_design [get_files design_1.bd]
set clk1 [get_bd_pins zynq_ultra_ps_e_0/pl_clk1]
# 1. remember every load on the two CMAC user clocks
set loads {}
foreach cp {qsfp_cmac/gt_txusrclk2 qsfp_cmac/gt_rxusrclk2} {
  foreach p [get_bd_pins -of [get_bd_nets -of [get_bd_pins $cp]] -filter {DIR == I}] {
    if {![string match /qsfp_cmac/* $p]} { lappend loads $p }
  }
}
L "clock loads moved to pl_clk1: $loads"
# 2. remove CMAC, its RX reset inverter and the QSFP external ports
foreach n {qsfp_cmac/gt_txusrclk2 qsfp_cmac/gt_rxusrclk2} { delete_bd_objs [get_bd_nets -of [get_bd_pins $n]] }
delete_bd_objs [get_bd_intf_nets -of [get_bd_intf_pins qsfp_cmac/axis_rx]] [get_bd_intf_nets -of [get_bd_intf_pins qsfp_cmac/axis_tx]]
delete_bd_objs [get_bd_intf_nets -of [get_bd_intf_pins qsfp_cmac/s_axi]]
delete_bd_objs [get_bd_cells qsfp_cmac] [get_bd_cells qsfp_rx_resetn]
foreach ip {qsfp_gt qsfp_refclk} { catch {delete_bd_objs [get_bd_intf_nets -quiet -of [get_bd_intf_ports $ip]]}; delete_bd_objs [get_bd_intf_ports $ip] }
foreach p {qsfp_intl_ls qsfp_resetl_ls qsfp_lpmode_ls qsfp_modsell_ls} {
  if {[get_bd_ports -quiet $p] eq ""} { continue }
  set n [get_bd_nets -quiet -of [get_bd_ports $p]]
  L "port $p net=$n"
  if {$n ne ""} { disconnect_bd_net $n [get_bd_ports $p] }
  delete_bd_objs [get_bd_ports $p]
}
foreach n [get_bd_nets -quiet] { if {[llength [get_bd_pins -quiet -of $n]] + [llength [get_bd_ports -quiet -of $n]] < 2} { L "removing dangling net $n"; delete_bd_objs $n } }
# 3. the whole NetworkLayer domain now runs on pl_clk1 (125 MHz)
foreach p $loads { if {[get_bd_pins -quiet $p] ne ""} { connect_bd_net $clk1 [get_bd_pins $p] } }
set_property -dict [list CONFIG.C_EXT_RESET_HIGH {0}] [get_bd_cells qsfp_reset]
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_resetn0] [get_bd_pins qsfp_reset/ext_reset_in]
set nl_rstn [get_bd_pins qsfp_reset/peripheral_aresetn]
set_property -dict [list CONFIG.IS_ACLK_ASYNC {0} CONFIG.TUSER_WIDTH {0}] [get_bd_cells qsfp_rx_mac_fifo]
connect_bd_net $nl_rstn [get_bd_pins qsfp_rx_mac_fifo/s_axis_aresetn]
# 4. SG DMA pair + 64<->512 width converters
set dma [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_dma:7.1 nl_eth_dma]
set_property -dict [list CONFIG.c_include_sg {1} CONFIG.c_sg_include_stscntrl_strm {0} CONFIG.c_sg_length_width {16} \
  CONFIG.c_include_mm2s {1} CONFIG.c_include_s2mm {1} CONFIG.c_m_axi_mm2s_data_width {64} CONFIG.c_m_axis_mm2s_tdata_width {64} \
  CONFIG.c_m_axi_s2mm_data_width {64} CONFIG.c_include_mm2s_dre {1} CONFIG.c_include_s2mm_dre {1} \
  CONFIG.c_mm2s_burst_size {16} CONFIG.c_s2mm_burst_size {16} CONFIG.c_addr_width {32} CONFIG.c_prmry_is_aclk_async {1}] $dma
set up [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_dwidth_converter:1.1 nl_eth_tx_up]
set_property -dict [list CONFIG.S_TDATA_NUM_BYTES {8} CONFIG.M_TDATA_NUM_BYTES {64} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1}] $up
set dn [create_bd_cell -type ip -vlnv xilinx.com:ip:axis_dwidth_converter:1.1 nl_eth_rx_dn]
set_property -dict [list CONFIG.S_TDATA_NUM_BYTES {64} CONFIG.M_TDATA_NUM_BYTES {8} CONFIG.HAS_TKEEP {1} CONFIG.HAS_TLAST {1}] $dn
connect_bd_intf_net [get_bd_intf_pins nl_eth_dma/M_AXIS_MM2S] [get_bd_intf_pins nl_eth_tx_up/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins nl_eth_tx_up/M_AXIS] [get_bd_intf_pins qsfp_rx_mac_fifo/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins qsfp_tx_mac_fifo/M_AXIS] [get_bd_intf_pins nl_eth_rx_dn/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins nl_eth_rx_dn/M_AXIS] [get_bd_intf_pins nl_eth_dma/S_AXIS_S2MM]
foreach c {nl_eth_tx_up nl_eth_rx_dn} { connect_bd_net $clk1 [get_bd_pins $c/aclk]; connect_bd_net $nl_rstn [get_bd_pins $c/aresetn] }
connect_bd_net $clk1 [get_bd_pins nl_eth_dma/m_axi_sg_aclk] [get_bd_pins nl_eth_dma/m_axi_mm2s_aclk] [get_bd_pins nl_eth_dma/m_axi_s2mm_aclk]
connect_bd_net [get_bd_pins zynq_ultra_ps_e_0/pl_clk0] [get_bd_pins nl_eth_dma/s_axi_lite_aclk]
connect_bd_net [get_bd_pins reset_block/peripheral_aresetn] [get_bd_pins nl_eth_dma/axi_resetn]
connect_bd_intf_net [get_bd_intf_pins axi_interconnect_7/M01_AXI] [get_bd_intf_pins nl_eth_dma/S_AXI_LITE]
# 5. memory side: dedicated SmartConnect -> S_AXI_HP1_FPD
set_property -dict [list CONFIG.PSU__USE__S_AXI_GP3 {1} CONFIG.PSU__SAXIGP3__DATA_WIDTH {64}] [get_bd_cells zynq_ultra_ps_e_0]
set smc [create_bd_cell -type ip -vlnv xilinx.com:ip:smartconnect:1.0 nl_eth_smc]
set_property -dict [list CONFIG.NUM_SI {3} CONFIG.NUM_MI {1} CONFIG.NUM_CLKS {1}] $smc
connect_bd_intf_net [get_bd_intf_pins nl_eth_dma/M_AXI_SG]   [get_bd_intf_pins nl_eth_smc/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins nl_eth_dma/M_AXI_MM2S] [get_bd_intf_pins nl_eth_smc/S01_AXI]
connect_bd_intf_net [get_bd_intf_pins nl_eth_dma/M_AXI_S2MM] [get_bd_intf_pins nl_eth_smc/S02_AXI]
connect_bd_intf_net [get_bd_intf_pins nl_eth_smc/M00_AXI] [get_bd_intf_pins zynq_ultra_ps_e_0/S_AXI_HP1_FPD]
connect_bd_net $clk1 [get_bd_pins nl_eth_smc/aclk] [get_bd_pins zynq_ultra_ps_e_0/saxihp1_fpd_aclk]
connect_bd_net [get_bd_pins reset_block/peripheral_aresetn4] [get_bd_pins nl_eth_smc/aresetn]
# 6. addresses
assign_bd_address -offset 0xB0100000 -range 0x00010000 -target_address_space [get_bd_addr_spaces zynq_ultra_ps_e_0/Data] [get_bd_addr_segs nl_eth_dma/S_AXI_LITE/Reg] -force
assign_bd_address
foreach sp {Data_SG Data_MM2S Data_S2MM} {
  foreach s [get_bd_addr_segs -quiet -of [get_bd_addr_spaces nl_eth_dma/$sp]] {
    if {![string match *DDR_LOW* $s]} { exclude_bd_addr_seg $s } else { L "$sp -> $s [get_property OFFSET $s] [get_property RANGE $s]" }
  }
}
L "unassigned: [get_bd_addr_segs -quiet -filter {NAME =~ *unassigned*}]"
L "stripper pins: [get_bd_pins qsfp_eth_rx_pad_stripper/*]"
set rc [catch {validate_bd_design} msg]; L "validate rc=$rc msg=$msg"
save_bd_design
# 7. constraints
set old [get_files -quiet -of [get_filesets constrs_1] *rfsoc4x2_qsfp100g.xdc]
if {$old ne ""} { remove_files -fileset constrs_1 $old }
add_files -fileset constrs_1 -norecurse $W/src/constraints/rfsoc4x2_nlsg.xdc
L "constrs: [get_files -of [get_filesets constrs_1]]"
L "DONE_BD_EDIT validate_rc=$rc"
} err]} { L "FAILED: $err"; L $::errorInfo }
close $LOG
