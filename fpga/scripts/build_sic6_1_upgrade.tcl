# helix_nlsg_sic6: the HELIX split-6 design (fpga/p) + 8-tap SIC + SSB IP 1.3 + stream_manager 1.2.
#   SSB_TOP_v3 1.3 (fpga/ip_local/SSB_v3_5G_IP_v1_3): slot hold-off (196 us default) and runtime PSS0/PSS1 detection with
#     discrimination; SSB AXI-lite R2 (0xB0009008) bit0 = detect PSS1, bit1 = no discrimination (R2 = 0: PSS0 as before).
#   stream_manager 1.2 (fpga/ip_local/stream_manager_1_2): the TX packet_fragmentation keeps a trigger (and its transfer size)
#     that arrives while a transfer is still running instead of losing it, and reports busy (reg0 bit2) so that
#     board/helix_ctrl_service.py holds the next trigger until the previous transfer has finished.
# Needs the Release asset helix-build-ip.tar.gz unpacked in the repository root (fpga/ip_local, fpga/vendor, fpga/hls_ldpc_tx/prj3).
# From the repository root, two runs (Vivado 2024.1 crashes in the same session after upgrading these IPs in the block design):
#   vivado -mode batch -source fpga/scripts/build_sic6_1_upgrade.tcl
#   vivado -mode batch -source fpga/scripts/build_sic6_2_impl.tcl
# Progress: fpga/build_status_sic6.txt.  Output: fpga/out/helix_nlsg_sic6.bit/.hwh
set W [file normalize [file join [file dirname [info script]] ..]]
proc S {m} { global W; set f [open $W/build_status_sic6.txt a]; puts $f "[clock format [clock seconds] -format %H:%M:%S] $m"; close $f }
if {[catch {
open_project $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.xpr
set_property ip_repo_paths [list [file join $W ip_local] [file join $W vendor helix IP_REPO] [file join $W vendor network networklayer_rfsoc4x2_2024_1_2] [file join $W vendor network networklayer_hls_2024_1_2] [file join $W hls_ldpc_tx prj3 sol1 impl ip]] [current_project]
update_ip_catalog -rebuild
set_param sta.disableNewWireDlyCalc true
set_param general.maxThreads 8
config_ip_cache -disable_cache
set sr [get_runs synth_1]; set ir [get_runs impl_1]
set_property STRATEGY Flow_PerfOptimized_high $sr
set_property STRATEGY Performance_ExplorePostRoutePhysOpt $ir
foreach cfg [get_report_configs -quiet impl_1_route_report_methodology_0] { set_property IS_ENABLED 0 $cfg }
open_bd_design [get_files design_1.bd]
if {[get_bd_cells -quiet sic_0] eq ""} { error "sic_0 is not in the block design (run 06_add_sic.tcl first)" }
foreach {vlnv cell} {xilinx.com:user:SSB_TOP_v3:1.3 rx_blocks/SSB_TOP_v3_0 user.org:user:stream_manager:1.2 stream_manager_0} {
  if {[llength [get_ipdefs -quiet $vlnv]] != 1} { error "$vlnv is not in the IP catalog (unpack the build IP archive)" }
  set c [get_bd_cells $cell]
  S "$cell before: [get_property VLNV $c]"
  if {[get_property VLNV $c] ne $vlnv} {
    upgrade_ip -vlnv $vlnv [get_ips [get_property CONFIG.Component_Name $c]] -log $W/ip_upgrade_sic6.log
    validate_bd_design
    save_bd_design
  }
  S "$cell after: [get_property VLNV [get_bd_cells $cell]]"
  if {[get_property VLNV [get_bd_cells $cell]] ne $vlnv} { error "upgrade of $cell to $vlnv failed" }
}
S "UPGRADE_DONE (now run build_sic6_2_impl.tcl)"
} err]} { S "UPGRADE_FAILED: $err"; puts stderr $err; exit 1 }
exit
