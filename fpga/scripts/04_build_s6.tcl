# Claude 2026-11-11: build nlsg + TX split-6 front end (ldpc_tx_enc) -> N:/out/helix_nlsg_s6.bit/.hwh
# Run (N: = subst of <project>\work\nlsg, project closed elsewhere):
#   C:\Xilinx\Vivado\2024.1\bin\vivado.bat -mode batch -source N:/scripts/04_build_s6.tcl -log N:/build_s6.log -journal N:/build_s6.jou
# Progress: N:/build_status_s6.txt.  The previous bit (N:/out/helix_nlsg.bit) is not touched.
set W N:
proc S {m} { global W; set f [open $W/build_status_s6.txt a]; puts $f "[clock format [clock seconds] -format %H:%M:%S] $m"; close $f }
if {[catch {
open_project $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.xpr
set C8 {C:/Users/asd32/Documents/Codex/2026-08-03/c-c-users-asd32-documents-codex-6}
set_property ip_repo_paths [list $C8/work/hq46/helix/fpga/IP_REPO $C8/outputs/helix_rfsoc4x2_qsfp100g_v46_2024_1_2/ip_repo/networklayer_rfsoc4x2_2024_1_2 $C8/outputs/helix_rfsoc4x2_qsfp100g_v46_2024_1_2/ip_repo/networklayer_hls_2024_1_2 {C:/Users/asd32/Documents/Codex/2026-10-31/helix-rfsoc-4x2-qsfp-codex/work/nlsg/hls_ldpc_tx/prj2/sol1/impl/ip}] [current_project]
update_ip_catalog -rebuild
set_param sta.disableNewWireDlyCalc true
set_param general.maxThreads 8
config_ip_cache -disable_cache
set sr [get_runs synth_1]; set ir [get_runs impl_1]
set_property STRATEGY Flow_PerfOptimized_high $sr
set_property STRATEGY Performance_ExplorePostRoutePhysOpt $ir
foreach cfg [get_report_configs -quiet impl_1_route_report_methodology_0] { set_property IS_ENABLED 0 $cfg }
open_bd_design [get_files design_1.bd]
if {[get_bd_cells -quiet tx_blocks/ldpc_tx_enc_0] eq ""} { error "ldpc_tx_enc_0 is not in the block design (run 03_add_ldpc_tx.tcl first)" }
S "locked IPs: [llength [get_ips -quiet -filter {IS_LOCKED == 1}]]; generate_target"
generate_target all [get_files design_1.bd]
S "generate_target done"
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
file copy -force $bit $od/helix_nlsg_s6.bit
file copy -force $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh $od/helix_nlsg_s6.hwh
S "BUILD_DONE bit=$od/helix_nlsg_s6.bit"
} err]} { S "BUILD_FAILED: $err" }
exit
