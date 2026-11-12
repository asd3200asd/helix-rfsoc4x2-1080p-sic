set bin [file join $::env(XILINX_VIVADO) bin]
set rc1 [catch {exec cmd /c [file join $bin xvlog.bat] --sv ../rtl/sic_cfir2x.sv ../rtl/sic_ref_align.sv ../rtl/sic_monitor.sv ../rtl/sic_top.sv ../rtl/sic_axil.sv ../rtl/sic_axil_bd.v ../tb/tb_sic_axil.sv > axil_xvlog.log 2>@1} m]
set rc2 [catch {exec cmd /c [file join $bin xelab.bat] tb_sic_axil -s snap_axil > axil_xelab.log 2>@1} m]
set rc3 [catch {exec cmd /c [file join $bin xsim.bat] snap_axil -R > axil_xsim.log 2>@1} m]
puts "rc $rc1 $rc2 $rc3"
