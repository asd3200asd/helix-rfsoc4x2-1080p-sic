# Claude 2026-11-12: sic1 + slot hold-off in the SSB block (local IP copy N:/ip_local/SSB_v3_5G_IP_v1_1, VLNV SSB_TOP_v3:1.1;
# packet_sync accepts a detection only when idle and >= 165 us after the previous accepted slot start; R1[31:17] overrides) -> N:/out/helix_nlsg_sic2.bit/.hwh
# The shared IP repo is not modified. BD backup: design_1.bd.bak_pre_holdoff
# Run: C:\Xilinx\Vivado\2024.1\bin\vivado.bat -mode batch -source N:/scripts/08_build_sic2.tcl -log N:/build_sic2.log -journal N:/build_sic2.jou
# Progress: N:/build_status_sic2.txt. Existing bits in N:/out are not touched.
set W N:
proc S {m} { global W; set f [open $W/build_status_sic2.txt a]; puts $f "[clock format [clock seconds] -format %H:%M:%S] $m"; close $f }
if {[catch {
open_project $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.xpr
set C8 {C:/Users/asd32/Documents/Codex/2026-08-03/c-c-users-asd32-documents-codex-6}
set_property ip_repo_paths [list $W/ip_local $C8/work/hq46/helix/fpga/IP_REPO $C8/outputs/helix_rfsoc4x2_qsfp100g_v46_2024_1_2/ip_repo/networklayer_rfsoc4x2_2024_1_2 $C8/outputs/helix_rfsoc4x2_qsfp100g_v46_2024_1_2/ip_repo/networklayer_hls_2024_1_2 {C:/Users/asd32/Documents/Codex/2026-10-31/helix-rfsoc-4x2-qsfp-codex/work/nlsg/hls_ldpc_tx/prj3/sol1/impl/ip}] [current_project]
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
set ssb [get_bd_cells rx_blocks/SSB_TOP_v3_0]
S "SSB cell VLNV before: [get_property VLNV $ssb]"
if {[get_property VLNV $ssb] ne "xilinx.com:user:SSB_TOP_v3:1.1"} {
  set bdf [get_files design_1.bd]; file copy -force $bdf [file rootname $bdf].bd.bak_pre_holdoff
  upgrade_ip -vlnv xilinx.com:user:SSB_TOP_v3:1.1 [get_ips design_1_SSB_TOP_v3_0_0] -log $W/ip_upgrade_sic2.log
  validate_bd_design
  save_bd_design
}
S "SSB cell VLNV after: [get_property VLNV [get_bd_cells rx_blocks/SSB_TOP_v3_0]]"
if {[get_property VLNV [get_bd_cells rx_blocks/SSB_TOP_v3_0]] ne "xilinx.com:user:SSB_TOP_v3:1.1"} { error "SSB IP upgrade failed" }
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
set ps [glob -nocomplain $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.gen/sources_1/bd/design_1/ipshared/*/src/packet_sync.vhd]
set ok 0; foreach f $ps { set h [open $f]; if {[string first HOLDOFF_DEFAULT [read $h]] >= 0} { set ok 1 }; close $h }
S "generated packet_sync copies: $ps ; hold-off present: $ok"
if {!$ok} { error "generated SSB sources do not contain the hold-off" }
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
file copy -force $bit $od/helix_nlsg_sic2.bit
file copy -force $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh $od/helix_nlsg_sic2.hwh
S "BUILD_DONE bit=$od/helix_nlsg_sic2.bit"
} err]} { S "BUILD_FAILED: $err" }
exit
