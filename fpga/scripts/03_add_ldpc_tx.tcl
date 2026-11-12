# Claude 2026-11-11: insert the HLS TX split-6 front end (claude:helix:ldpc_tx_enc:1.0) into the nlsg block design.
# Run in Vivado 2024.1.2 with N:/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.xpr and design_1.bd open, after setting ip_repo_paths
# to the absolute v46 repos + work/nlsg/hls_ldpc_tx/prj2/sol1/impl/ip.   source N:/scripts/03_add_ldpc_tx.tcl
# Result: tx_buffer_top_0.m00_axis -> ldpc_tx_enc_0 -> TX axis switch S00; AXI-Lite at 0xB0011000 (axi_interconnect_7/M27).
# Next: 04_build_s6.tcl (synth -> impl -> out/helix_nlsg_s6.bit/.hwh).
set bdf [get_files design_1.bd]
file copy -force $bdf [file rootname $bdf].bd.bak_pre_ldpc
set c [create_bd_cell -type ip -vlnv claude:helix:ldpc_tx_enc:1.0 tx_blocks/ldpc_tx_enc_0]
delete_bd_objs [get_bd_intf_nets tx_blocks/tx_buffer_top_0_m00_axis]
connect_bd_intf_net [get_bd_intf_pins tx_blocks/tx_buffer_top_0/m00_axis] [get_bd_intf_pins tx_blocks/ldpc_tx_enc_0/s_axis]
connect_bd_intf_net [get_bd_intf_pins tx_blocks/ldpc_tx_enc_0/m_axis] [get_bd_intf_pins tx_blocks/axis_interconnect_0/S00_AXIS]
connect_bd_net [get_bd_pins tx_blocks/aclk] [get_bd_pins tx_blocks/ldpc_tx_enc_0/ap_clk]
connect_bd_net [get_bd_pins tx_blocks/soft_reset/Res] [get_bd_pins tx_blocks/ldpc_tx_enc_0/ap_rst_n]
set_property CONFIG.NUM_MI 28 [get_bd_cells axi_interconnect_7]
connect_bd_net -net [get_bd_nets s_axis_aclk1_1] [get_bd_pins axi_interconnect_7/M27_ACLK]
connect_bd_net -net [get_bd_nets ARESETN_1] [get_bd_pins axi_interconnect_7/M27_ARESETN]
create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 tx_blocks/s_axi_ldpc
connect_bd_intf_net [get_bd_intf_pins tx_blocks/s_axi_ldpc] [get_bd_intf_pins tx_blocks/ldpc_tx_enc_0/s_axi_ctrl]
connect_bd_intf_net [get_bd_intf_pins axi_interconnect_7/M27_AXI] [get_bd_intf_pins tx_blocks/s_axi_ldpc]
assign_bd_address -offset 0xB0011000 -range 0x1000 [get_bd_addr_segs tx_blocks/ldpc_tx_enc_0/s_axi_ctrl/Reg]
puts "LDPC_BD_EDIT_DONE [get_bd_addr_segs -of_objects [get_bd_addr_spaces zynq_ultra_ps_e_0/Data] -filter {NAME =~ *ldpc*}]"
