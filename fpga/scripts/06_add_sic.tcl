# Claude 2026-11-11 (SIC conversation): insert the clean post-DDC SIC (work/sic_clean, copied to N:/src/rtl/sic)
# between the RF-ADC I/Q streams and rx_blocks. After reset the SIC is disabled = bit-exact bypass (8 clocks latency).
#   RFDC m20_axis(I)/m21_axis(Q) -> sic_0 -> rx_blocks/s00_axis/s01_axis ; reference = TxUpsampleIFS m00_axis_tdata
#   AXI-Lite 0xB0013000 (axi_interconnect_7/M28, adc_clk). BD backup: design_1.bd.bak_pre_sic
# Run: vivado -mode batch/tcl, source N:/scripts/06_add_sic.tcl   (status: N:/sic_bd_status.txt)
# NOTE: this script alone leaves the RF-DAC tdata input unconnected (BD 41-1306); 07_build_sic1.tcl adds the explicit net.
set W N:
proc S {m} { global W; set f [open $W/sic_bd_status.txt a]; puts $f "[clock format [clock seconds] -format %H:%M:%S] $m"; close $f }
if {[catch {
open_project $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.xpr
S "project open; locked IPs: [llength [get_ips -quiet -filter {IS_LOCKED == 1}]]"
add_files -norecurse [list $W/src/rtl/sic/sic_cfir2x.sv $W/src/rtl/sic/sic_ref_align.sv $W/src/rtl/sic/sic_monitor.sv $W/src/rtl/sic/sic_axil.sv $W/src/rtl/sic/sic_axil_bd.v]
update_compile_order -fileset sources_1
set bdf [get_files design_1.bd]
open_bd_design $bdf
if {[get_bd_cells -quiet sic_0] ne ""} { error "sic_0 already exists" }
file copy -force $bdf [file rootname $bdf].bd.bak_pre_sic
create_bd_cell -type module -reference sic_axil_bd sic_0
set_property -dict [list CONFIG.TAPS 8 CONFIG.DW 12 CONFIG.WLOG 22 CONFIG.CAP_AW 12] [get_bd_cells sic_0]
S "cell created: [get_bd_intf_pins sic_0/*]"
delete_bd_objs [get_bd_intf_nets s00_axis_1] [get_bd_intf_nets s01_axis_2]
connect_bd_intf_net [get_bd_intf_pins usp_rf_data_converter_0/m20_axis] [get_bd_intf_pins sic_0/s_axis_i]
connect_bd_intf_net [get_bd_intf_pins usp_rf_data_converter_0/m21_axis] [get_bd_intf_pins sic_0/s_axis_q]
connect_bd_intf_net [get_bd_intf_pins sic_0/m_axis_i] [get_bd_intf_pins rx_blocks/s00_axis]
connect_bd_intf_net [get_bd_intf_pins sic_0/m_axis_q] [get_bd_intf_pins rx_blocks/s01_axis]
connect_bd_net -net [get_bd_nets MTS_Block_adc_clk] [get_bd_pins sic_0/aclk]
connect_bd_net -net [get_bd_nets reset_block_peripheral_aresetn2] [get_bd_pins sic_0/aresetn]
connect_bd_net -net [get_bd_nets s_axis_aclk1_1] [get_bd_pins sic_0/dac_clk]
create_bd_pin -dir O -from 63 -to 0 tx_blocks/dac_tdata_tap
connect_bd_net [get_bd_pins tx_blocks/TxUpsampleIFS_5G_IP_0/m00_axis_tdata] [get_bd_pins tx_blocks/dac_tdata_tap]
connect_bd_net [get_bd_pins tx_blocks/dac_tdata_tap] [get_bd_pins sic_0/dac_tdata]
set_property CONFIG.NUM_MI 29 [get_bd_cells axi_interconnect_7]
connect_bd_net -net [get_bd_nets MTS_Block_adc_clk] [get_bd_pins axi_interconnect_7/M28_ACLK]
connect_bd_net -net [get_bd_nets reset_block_peripheral_aresetn2] [get_bd_pins axi_interconnect_7/M28_ARESETN]
connect_bd_intf_net [get_bd_intf_pins axi_interconnect_7/M28_AXI] [get_bd_intf_pins sic_0/s_axi]
set seg [get_bd_addr_segs sic_0/s_axi/*]
S "addr segs: $seg"
assign_bd_address -offset 0xB0013000 -range 0x1000 $seg
S "validate"
validate_bd_design
save_bd_design
S "SIC_BD_EDIT_DONE [get_bd_addr_segs -of_objects [get_bd_addr_spaces zynq_ultra_ps_e_0/Data] -filter {NAME =~ *sic*}]"
} err]} { S "SIC_BD_EDIT_FAILED: $err" }
