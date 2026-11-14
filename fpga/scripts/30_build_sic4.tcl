# Claude 2026-11-14: sic4 = sic3 + SSB IP 1.3 (runtime PSS0/PSS1 select in R2(0) of the SSB AXI-lite block, PSS0/PSS1 discrimination
# unless R2(1) = 1; R2 = 0 keeps the sic3 behaviour plus discrimination).  Builds in a copy N:/p_sic4 (N:/p is not modified).
# Run: C:\Xilinx\Vivado\2024.1\bin\vivado.bat -mode batch -source N:/scripts/30_build_sic4.tcl -log N:/build_sic4.log -journal N:/build_sic4.jou
# Progress: N:/build_status_sic4.txt.  Output: N:/out/helix_nlsg_sic4.bit/.hwh
set W N:
proc S {m} { global W; set f [open $W/build_status_sic4.txt a]; puts $f "[clock format [clock seconds] -format %H:%M:%S] $m"; close $f }
if {[catch {
S "package SSB IP 1.3"
ipx::open_core $W/ip_local/SSB_v3_5G_IP_v1_3/component.xml
set core [ipx::current_core]
S "core [get_property VLNV $core]"
ipx::update_checksums $core
ipx::save_core $core
ipx::unload_core $core
open_project $W/p/HELIX_RFSoC4x2_QSFP100G_V46_PSDMA.xpr
if {[file exists $W/p_sic4]} { error "N:/p_sic4 exists" }
save_project_as -include_local_ip_cache sic4 $W/p_sic4
S "project: [get_property DIRECTORY [current_project]]"
if {![string match -nocase *p_sic4* [get_property DIRECTORY [current_project]]]} { error "not in the copied project" }
update_ip_catalog -rebuild
if {[llength [get_ipdefs -quiet xilinx.com:user:SSB_TOP_v3:1.3]] != 1} { error "SSB_TOP_v3:1.3 not in the IP catalog" }
set_param sta.disableNewWireDlyCalc true
set_param general.maxThreads 8
config_ip_cache -disable_cache
set sr [get_runs synth_1]; set ir [get_runs impl_1]
set_property STRATEGY Flow_PerfOptimized_high $sr
set_property STRATEGY Performance_ExplorePostRoutePhysOpt $ir
foreach cfg [get_report_configs -quiet impl_1_route_report_methodology_0] { set_property IS_ENABLED 0 $cfg }
open_bd_design [get_files design_1.bd]
S "SSB before: [get_property VLNV [get_bd_cells rx_blocks/SSB_TOP_v3_0]]"
upgrade_ip -vlnv xilinx.com:user:SSB_TOP_v3:1.3 [get_ips design_1_SSB_TOP_v3_0_0] -log $W/ip_upgrade_sic4.log
validate_bd_design
save_bd_design
S "SSB after: [get_property VLNV [get_bd_cells rx_blocks/SSB_TOP_v3_0]]"
if {[get_property VLNV [get_bd_cells rx_blocks/SSB_TOP_v3_0]] ne "xilinx.com:user:SSB_TOP_v3:1.3"} { error "SSB upgrade failed" }
generate_target all [get_files design_1.bd]
set g [glob -nocomplain $W/p_sic4/*.gen/sources_1/bd/design_1/ipshared/*/src/PSS_RUNTIME_DETECTION_TOP.vhd]
set ok 0; foreach f $g { set h [open $f]; if {[string first {pss_disc_en} [read $h]] >= 0} { set ok 1 }; close $h }
S "generated runtime detector: $g ; discrimination present: $ok"
if {!$ok} { error "generated SSB sources lack the discrimination" }
set_property top design_1_wrapper [get_filesets sources_1]
update_compile_order -fileset sources_1
set n 0
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
file copy -force $bit $od/helix_nlsg_sic4.bit
file copy -force [lindex [glob $W/p_sic4/*.gen/sources_1/bd/design_1/hw_handoff/design_1.hwh] 0] $od/helix_nlsg_sic4.hwh
S "BUILD_DONE bit=$od/helix_nlsg_sic4.bit"
} err]} { S "BUILD_FAILED: $err" }
exit