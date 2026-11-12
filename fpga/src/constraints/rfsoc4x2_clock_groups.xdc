# PS PL clocks and the external 122.88 MHz MTS clock are independent sources.
# All intentional traffic between these families uses synchronizers or async
# AXI/AXIS FIFOs.  Read this file with PROCESSING_ORDER=LATE so all generated
# clocks from the MTS clock wizard already exist.
set_clock_groups -name ps_mts_async -asynchronous \
    -group [get_clocks -quiet {clk_pl_0 clk_pl_1}] \
    -group [get_clocks -quiet -include_generated_clocks mts_pl_clk_122p88]
