# Out-of-context synthesis of sic_top alone (does not touch the nlsg project).
set here [file dirname [file normalize [info script]]]
foreach t {4 8} {
    read_verilog -sv [glob $here/../rtl/*.sv]
    synth_design -top sic_top -part xczu48dr-ffvg1517-2-e -mode out_of_context -generic TAPS=$t
    create_clock -name adc_clk -period 4.069 [get_ports adc_clk]
    create_clock -name dac_clk -period 4.069 [get_ports dac_clk]
    report_utilization -file $here/util_t$t.rpt
    report_timing_summary -file $here/timing_t$t.rpt
    close_design
}
