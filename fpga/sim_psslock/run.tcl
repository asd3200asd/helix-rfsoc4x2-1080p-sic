# Claude 2026-11-12.  In a Vivado Tcl shell:
#   cd <this dir>; set PSRC <packet_sync.vhd>; set OSRC <OFDM_demod_TOP_v3.vhd>; set TAG <name>; set EXTRAS {900000000 3000 ...}; source run.tcl
# EXTRA = clocks between the regular detection of slot 4 and the injected extra detection (900000000 = none).
set V C:/Xilinx/Vivado/2024.1/bin
set R C:/Users/asd32/Documents/Codex/2026-08-03/c-c-users-asd32-documents-codex-6/work/hq46/helix/fpga/IP_REPO/5G_TESTBED
set O $R/OFDM_DEMOD_V3_5G_IP/src
file mkdir build_$TAG; cd build_$TAG
set skip {OFDM_demod_TOP_v3_tb.vhd OFDM_demod_TOP_v3_wrp.vhd OFDM_DEMOD_AXI_LITE.vhd OFDM_demod_TOP_v3.vhd FFT_HDL_Optimized_Streaming_pkg.vhd}
set prj [open files.prj w]
puts $prj "vhdl work \"$O/FFT_HDL_Optimized_Streaming_pkg.vhd\""
foreach f [lsort [glob $O/*.vhd]] { if {[lsearch $skip [file tail $f]] < 0} { puts $prj "vhdl work \"$f\"" } }
foreach f [list $OSRC $PSRC ../PACKET_SYNC_MEM_sim.vhd ../sim_models.vhd] { puts $prj "vhdl work \"$f\"" }
close $prj
set log [open compile.log w]
proc run {log args} { if {[catch {exec {*}$args 2>@1} r]} { puts $log "FAIL: [lrange $args 0 3]\n[string range $r end-1500 end]" } else { puts $log "OK: [lrange $args 0 3]" }; flush $log }
run $log $V/xvhdl.bat -prj files.prj
foreach e $EXTRAS {
    set c [open tb_cfg.vhd w]; puts $c "package tb_cfg is constant EXTRA : integer := $e; end package;"; close $c
    run $log $V/xvhdl.bat tb_cfg.vhd ../tb_psslock.vhd
    run $log $V/xelab.bat work.tb_psslock -s s_$e
    run $log $V/xsim.bat s_$e -runall -log xsim_$e.log
}
close $log; cd ..
