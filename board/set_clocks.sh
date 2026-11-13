#!/usr/bin/env bash
# RFSoC4x2 LMK/LMX reference clocks (245.76 / 491.52 MHz); once after boot, before loading the bit.
sudo env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B -u -c "import xrfclk; xrfclk.set_ref_clks(lmk_freq=245.76, lmx_freq=491.52); print('CLOCKS_SET')" 2>&1 | tail -1
