# Claude 2026-11-14: sic5 = sic4 + stream_manager 1.1 (TX trigger latch, see packet_fragmentation.v).  Former header: sic4 = sic3 + SSB IP 1.3 (runtime PSS0/PSS1 select in R2(0) of the SSB AXI-lite block, PSS0/PSS1 discrimination
# unless R2(1) = 1; R2 = 0 keeps the sic3 behaviour plus discrimination).  Builds in a copy N:/p_sic4 (N:/p is not modified).
# Run: C:\Xilinx\Vivado\2024.1\bin\vivado.bat -mode batch -source N:/scripts/30_build_sic4.tcl -log N:/build_sic4.log -journal N:/build_sic4.jou
# Progress: N:/build_status_sic5.txt.  Output: N:/out/helix_nlsg_sic5.bit/.hwh
set W N:
proc S {m} { global W; set f [open $W/build_status_sic5.txt a]; puts $f "[clock format [clock seconds] -format %H:%M:%S] $m"; close $f }
if {[catch {
S "package stream_manager 1.1"
open_project $W/p_sic5/sic5.xpr
S "resume: [get_property DIRECTORY [current_project]]"
update_ip_catalog
set_param sta.disableNewWireDlyCalc true
set_param general.maxThreads 8
config_ip_cache -disable_cache
set sr [get_runs synth_1]; set ir [get_runs impl_1]
set_property STRATEGY Flow_PerfOptimized_high $sr
set_property STRATEGY Performance_ExplorePostRoutePhysOpt $ir
foreach cfg [get_report_configs -quiet impl_1_route_report_methodology_0] { set_property IS_ENABLED 0 $cfg }
open_bd_design [get_files design_1.bd]
set smc [get_bd_cells -hier -filter {VLNV =~ "user.org:user:stream_manager:*"}]
S "stream_manager [get_property VLNV $smc]"
foreach cl $smc { if {[get_property VLNV $cl] ne "user.org:user:stream_manager:1.1"} { error "stream_manager is not 1.1" } }
generate_target all [get_files design_1.bd]
set g [glob -nocomplain $W/p_sic5/*.gen/sources_1/bd/design_1/ipshared/*/hdl/packet_fragmentation.v]
set ok 0; foreach f $g { set h [open $f]; if {[string first {LATCH_TRIGGER} [read $h]] >= 0} { set ok 1 }; close $h }
S "generated packet_fragmentation: $g ; latch present: $ok"
if {!$ok} { error "generated sources lack the trigger latch" }
set_property top design_1_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
set n 0
foreach r [get_runs -quiet -filter {NAME =~ *stream_manager*}] { catch {reset_run $r}; S "  reset $r" }
foreach r [get_runs -filter {IS_SYNTHESIS == 1}] { if {[get_property NEEDS_REFRESH $r] || ![string match *Complete* [get_property STATUS $r]]} { catch {reset_run $r}; incr n; S "  resynth $r" } }
catch {reset_run $sr}
S "launch synth ($n runs reset)"
launch_runs $sr -jobs 6
wait_on_run $sr
S "synth_1: [get_property STATUS $sr]"
if {![string match *Complete* [get_property STATUS $sr]]} { error "synthesis failed" }
catch {reset_run $ir}
S "launch impl"
launch_runs $ir -to_step write_bitstream -jobs 6
wait_on_run $ir
S "impl_1: [get_property STATUS $ir] WNS=[get_property STATS.WNS $ir] TNS=[get_property STATS.TNS $ir] WHS=[get_property STATS.WHS $ir] THS=[get_property STATS.THS $ir]"
if {![string match *Complete* [get_property STATUS $ir]]} { error "implementation failed" }
set od $W/out; file mkdir $od
set bit [lindex [glob -nocomplain [get_property DIRECTORY $ir]/*.bit] 0]
file copy -force $bit $od/helix_nlsg_sic5.bit
file copy -force [lindex [glob $W/p_sic5/*.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh] 0] $od/helix_nlsg_sic5.hwh
S "BUILD_DONE bit=$od/helix_nlsg_sic5.bit"
} err]} { S "BUILD_FAILED: $err" }
exit