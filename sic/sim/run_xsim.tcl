# Run from Vivado Tcl:  cd <this dir>; source run_xsim.tcl
set bin [file join $::env(XILINX_VIVADO) bin]
set st [open run_status.txt w]
proc run {st name args} { set rc [catch {exec cmd /c {*}$args > $name.log 2>@1} msg]; puts $st "$name rc=$rc"; flush $st; return $rc }
run $st xvlog [file join $bin xvlog.bat] --sv ../rtl/sic_cfir2x.sv ../rtl/sic_ref_align.sv ../rtl/sic_monitor.sv ../rtl/sic_top.sv ../tb/tb_sic_top.sv
run $st xelab_t1 [file join $bin xelab.bat] tb_t1 -s snap_t1
run $st xelab_t4 [file join $bin xelab.bat] tb_t4 -s snap_t4
run $st xelab_t8 [file join $bin xelab.bat] tb_t8 -s snap_t8
run $st xelab_t16 [file join $bin xelab.bat] tb_t16 -s snap_t16
set a [open tb_args.txt w]; puts $a "../cases/c10_fit_t4/stim.txt c10_fit_t4.out 134"; close $a
run $st c10_fit_t4 [file join $bin xsim.bat] snap_t4 -R
set a [open tb_args.txt w]; puts $a "../cases/c10_fit_t8/stim.txt c10_fit_t8.out 134"; close $a
run $st c10_fit_t8 [file join $bin xsim.bat] snap_t8 -R
set a [open tb_args.txt w]; puts $a "../cases/c1_bypass/stim.txt c1_bypass.out 37"; close $a
run $st c1_bypass [file join $bin xsim.bat] snap_t4 -R
set a [open tb_args.txt w]; puts $a "../cases/c2_zero_coef/stim.txt c2_zero_coef.out 37"; close $a
run $st c2_zero_coef [file join $bin xsim.bat] snap_t4 -R
set a [open tb_args.txt w]; puts $a "../cases/c3_si_only/stim.txt c3_si_only.out 301"; close $a
run $st c3_si_only [file join $bin xsim.bat] snap_t4 -R
set a [open tb_args.txt w]; puts $a "../cases/c4_si_plus_desired/stim.txt c4_si_plus_desired.out 301"; close $a
run $st c4_si_plus_desired [file join $bin xsim.bat] snap_t4 -R
set a [open tb_args.txt w]; puts $a "../cases/c5_random_t4/stim.txt c5_random_t4.out 255"; close $a
run $st c5_random_t4 [file join $bin xsim.bat] snap_t4 -R
set a [open tb_args.txt w]; puts $a "../cases/c6_random_t8/stim.txt c6_random_t8.out 1024"; close $a
run $st c6_random_t8 [file join $bin xsim.bat] snap_t8 -R
set a [open tb_args.txt w]; puts $a "../cases/c7_random_t1/stim.txt c7_random_t1.out 0"; close $a
run $st c7_random_t1 [file join $bin xsim.bat] snap_t1 -R
set a [open tb_args.txt w]; puts $a "../cases/c8_random_t16/stim.txt c8_random_t16.out 4093"; close $a
run $st c8_random_t16 [file join $bin xsim.bat] snap_t16 -R
set a [open tb_args.txt w]; puts $a "../cases/c9_switch/stim.txt c9_switch.out 64"; close $a
run $st c9_switch [file join $bin xsim.bat] snap_t4 -R
puts $st DONE
close $st
