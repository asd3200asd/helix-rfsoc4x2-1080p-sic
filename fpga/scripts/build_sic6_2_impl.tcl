# helix_nlsg_sic6: the HELIX split-6 design (fpga/p) + 8-tap SIC + SSB IP 1.3 + stream_manager 1.2.
#   SSB_TOP_v3 1.3 (fpga/ip_local/SSB_v3_5G_IP_v1_3): slot hold-off (196 us default) and runtime PSS0/PSS1 detection with
#     discrimination; SSB AXI-lite R2 (0xB0009008) bit0 = detect PSS1, bit1 = no discrimination (R2 = 0: PSS0 as before).
#   stream_manager 1.2 (fpga/ip_local/stream_manager_1_2): the TX packet_fragmentation keeps a trigger (and its transfer size)
#     that arrives while a transfer is still running instead of losing it, and reports busy (reg0 bit2) so that
#     board/helix_ctrl_service.py holds the next trigger until the previous transfer has finished.
# Needs the Release asset helix-build-ip.tar.gz unpacked in the repository root (fpga/ip_local, fpga/vendor, fpga/hls_ldpc_tx/prj3).
# Second of two runs, see build_sic6_1_upgrade.tcl.
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
  if {[get_property VLNV [get_bd_cells $cell]] ne $vlnv} { error "$cell is not $vlnv (run build_sic6_1_upgrade.tcl first)" }
}
S "IP versions ok"
# Tapping m00_axis_tdata with a net removes it from the interface connection (BD 41-1306), so the RF-DAC input must be wired explicitly.
if {[lsearch [get_bd_pins -of_objects [get_bd_nets -of_objects [get_bd_pins sic_0/dac_tdata]]] /usp_rf_data_converter_0/s00_axis_tdata] < 0} {
  connect_bd_net [get_bd_pins tx_blocks/dac_tdata_tap] [get_bd_pins usp_rf_data_converter_0/s00_axis_tdata]
  validate_bd_design
  save_bd_design
  S "wired dac tdata net to usp_rf_data_converter_0/s00_axis_tdata"
}
S "dac tdata net pins: [get_bd_pins -of_objects [get_bd_nets -of_objects [get_bd_pins sic_0/dac_tdata]]]"
S "sic aclk busif: [get_property CONFIG.ASSOCIATED_BUSIF [get_bd_pins sic_0/aclk]]"
S "locked IPs: [llength [get_ips -quiet -filter {IS_LOCKED == 1}]]; generate_target"
generate_target all [get_files design_1.bd]
S "generate_target done"
set gen $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.gen/sources_1/bd/design_1/ipshared
foreach {pat key} {src/packet_sync.vhd {HOLDOFF_DEFAULT : integer := 48170} src/PSS_RUNTIME_DETECTION_TOP.vhd pss_disc_en hdl/packet_fragmentation.v {assign busy}} {
  set ok 0; foreach f [glob -nocomplain $gen/*/$pat] { set h [open $f]; if {[string first $key [read $h]] >= 0} { set ok 1 }; close $h }
  S "generated $pat contains '$key': $ok"
  if {!$ok} { error "generated sources lack $key" }
}
set_property source_mgmt_mode All [current_project]; catch {set_property AUTO_INCREMENTAL_CHECKPOINT 0 [get_runs synth_1]}; catch {set_property INCREMENTAL_CHECKPOINT {} [get_runs synth_1]}
set_property top design_1_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
if {[get_property TOP [get_filesets sources_1]] ne "design_1_wrapper"} { error "top is not design_1_wrapper" }
# only runs that are new or out of date are re-synthesised
set n 0
foreach r [get_runs -filter {IS_SYNTHESIS == 1}] { if {[get_property NEEDS_REFRESH $r] || ![string match *Complete* [get_property STATUS $r]]} { catch {reset_run $r}; incr n; S "  resynth $r" } }
catch {reset_run $sr}
S "launch synth ($n OOC/top runs reset)"
launch_runs $sr -jobs 6
wait_on_run $sr
S "synth_1: [get_property STATUS $sr]"
foreach r [get_runs -filter {IS_SYNTHESIS == 1}] { if {![string match *Complete* [get_property STATUS $r]]} { S "  NOT COMPLETE $r [get_property STATUS $r]" } }
if {![string match *Complete* [get_property STATUS $sr]]} { error "synthesis failed" }
S "launch impl"
catch {reset_run $ir}
launch_runs $ir -to_step write_bitstream -jobs 6
wait_on_run $ir
S "impl_1: [get_property STATUS $ir] WNS=[get_property STATS.WNS $ir] TNS=[get_property STATS.TNS $ir] WHS=[get_property STATS.WHS $ir] THS=[get_property STATS.THS $ir]"
if {![string match *Complete* [get_property STATUS $ir]]} { error "implementation failed" }
set od $W/out; file mkdir $od
set bit [lindex [glob -nocomplain [get_property DIRECTORY $ir]/*.bit] 0]
file copy -force $bit $od/helix_nlsg_sic6.bit
file copy -force $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh $od/helix_nlsg_sic6.hwh
S "BUILD_DONE bit=$od/helix_nlsg_sic6.bit"
} err]} { S "BUILD_FAILED: $err"; puts stderr $err; exit 1 }
exit
