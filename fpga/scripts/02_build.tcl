# Claude 2026-11-10: synth -> impl -> bitstream for work/nlsg (v46 + NetworkLayer-side SG DMA, no CMAC).
set W N:
proc S {m} { global W; set f [open $W/build_status.txt a]; puts $f "[clock format [clock seconds] -format %H:%M:%S] $m"; close $f }
if {[catch {
set_param sta.disableNewWireDlyCalc true
set_param general.maxThreads 8
config_ip_cache -disable_cache
set sr [get_runs synth_1]; set ir [get_runs impl_1]
set_property STRATEGY Flow_PerfOptimized_high $sr
set_property STRATEGY Performance_ExplorePostRoutePhysOpt $ir
foreach cfg [get_report_configs -quiet impl_1_route_report_methodology_0] { set_property IS_ENABLED 0 $cfg }
S "generate_target"
generate_target all [get_files design_1.bd]
S "generate_target done"
if {[get_files -quiet *design_1_wrapper.vhd] eq ""} { add_files -norecurse [make_wrapper -files [get_files design_1.bd] -top] }
set_property source_mgmt_mode All [current_project]; catch {set_property AUTO_INCREMENTAL_CHECKPOINT 0 [get_runs synth_1]}; catch {set_property INCREMENTAL_CHECKPOINT {} [get_runs synth_1]}
set_property top design_1_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
S "top=[get_property TOP [get_filesets sources_1]] wrapper=[get_files -quiet *design_1_wrapper.vhd]"
if {[get_property TOP [get_filesets sources_1]] ne "design_1_wrapper"} { error "top is not design_1_wrapper" }
# full clean rebuild: every OOC run is reset so nothing stale from the copied v46 tree is reused
foreach r [get_runs -filter {IS_SYNTHESIS == 1}] { catch {reset_run $r} }
set pend 0; foreach r [get_runs -filter {IS_SYNTHESIS == 1}] { if {![string match *Complete* [get_property STATUS $r]]} {incr pend} }; S "launch synth ($pend runs pending)"
launch_runs $sr -jobs 6
wait_on_run $sr
S "synth_1: [get_property STATUS $sr]"
foreach r [get_runs -filter {IS_SYNTHESIS == 1}] { if {![string match *Complete* [get_property STATUS $r]]} { S "  NOT COMPLETE $r [get_property STATUS $r]" } }
if {![string match *Complete* [get_property STATUS $sr]]} { error "synthesis failed" }
S "launch impl"
launch_runs $ir -to_step write_bitstream -jobs 6
wait_on_run $ir
S "impl_1: [get_property STATUS $ir] WNS=[get_property STATS.WNS $ir] TNS=[get_property STATS.TNS $ir] WHS=[get_property STATS.WHS $ir] THS=[get_property STATS.THS $ir]"
if {![string match *Complete* [get_property STATUS $ir]]} { error "implementation failed" }
set od $W/out; file mkdir $od
set bit [lindex [glob -nocomplain [get_property DIRECTORY $ir]/*.bit] 0]
file copy -force $bit $od/helix_nlsg.bit
file copy -force $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh $od/helix_nlsg.hwh
S "BUILD_DONE bit=$od/helix_nlsg.bit"
} err]} { S "BUILD_FAILED: $err" }
