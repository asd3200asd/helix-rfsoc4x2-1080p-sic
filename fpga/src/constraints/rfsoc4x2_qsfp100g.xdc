# RFSoC 4x2 QSFP28 / CMAC reference clock (Bank 128, 156.25 MHz).
set_property PACKAGE_PIN AA33 [get_ports qsfp_refclk_clk_p]
set_property PACKAGE_PIN AA34 [get_ports qsfp_refclk_clk_n]

# RFSoC 4x2 QSFP control pins. RESETL and MODSELL are active-low.
set_property -dict {PACKAGE_PIN AM22 IOSTANDARD LVCMOS18} [get_ports qsfp_intl_ls]
set_property -dict {PACKAGE_PIN AL21 IOSTANDARD LVCMOS18} [get_ports qsfp_resetl_ls]
set_property -dict {PACKAGE_PIN AN22 IOSTANDARD LVCMOS18} [get_ports qsfp_lpmode_ls]
set_property -dict {PACKAGE_PIN AK22 IOSTANDARD LVCMOS18} [get_ports qsfp_modsell_ls]

# RFSoC 4x2 external MTS clock and SYSREF pins.
set_property -dict {PACKAGE_PIN AN11 IOSTANDARD LVDS DQS_BIAS TRUE} [get_ports pl_clk_clk_p]
set_property -dict {PACKAGE_PIN AP11 IOSTANDARD LVDS}               [get_ports pl_clk_clk_n]
set_property -dict {PACKAGE_PIN AP18 IOSTANDARD LVDS DQS_BIAS TRUE} [get_ports pl_sysref_clk_p]
set_property -dict {PACKAGE_PIN AR18 IOSTANDARD LVDS}               [get_ports pl_sysref_clk_n]

# BD FREQ_HZ metadata does not create a top-level timing clock.  Without this
# explicit primary clock the complete external-MTS/HELIX domain is reported as
# no_clock even though the differential IBUFDS is present.
create_clock -name mts_pl_clk_122p88 -period 8.138020833 \
  -waveform {0.000000000 4.069010417} [get_ports pl_clk_clk_p]

# HELIX RF front-end SPI mux control.
set_property -dict {PACKAGE_PIN C11 IOSTANDARD LVCMOS12} [get_ports {GPIO_0_tri_o[0]}]
set_property -dict {PACKAGE_PIN B12 IOSTANDARD LVCMOS12} [get_ports {GPIO_0_tri_o[1]}]
