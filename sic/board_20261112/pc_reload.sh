#!/usr/bin/env bash
# Claude 2026-11-12: PC-side board, between full-duplex runs: stop the services, reload helix_nlsg_sic1.bit, restart the services.
# usage: [SICBIT=sic2] bash sic/fdx/pc_reload.sh <service_seconds>      (passwordless sudo)
cd /home/xilinx/claude_nlsg_20261110 || exit 1
for p in $(pgrep -f '[f]dx_sic_claude|[o]rchestrate'); do sudo kill -INT $p 2>/dev/null; done; sleep 1
for p in $(pgrep -f 'python3 .*([h]elix_ctrl_service|[n]l_tap_bridge)'); do sudo kill -TERM $p 2>/dev/null; done
for i in $(seq 1 20); do pgrep -f '[h]elix_ctrl_service|[n]l_tap_bridge' >/dev/null || break; sleep 0.5; done
sudo env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B -u stepA_load_probe_${SICBIT:-sic1}.py --load 2>&1 | grep -E 'LOADED|rror'
sudo env HELIX_DAC_VOP=40500 HELIX_CORE=1 HELIX_BUSY=1 HELIX_BIT=helix_nlsg_${SICBIT:-sic1}.bit bash -c 'echo x | bash run_stack.sh "$@"' _ $(( $1 + 5 )) $1 --skip-clocks 2>&1 | tail -1
