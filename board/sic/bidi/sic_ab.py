#!/usr/bin/env python3
# SIC on/off A/B during a running bidi session: power before/after the SIC block every monitor window, and the receiver counters.
import sys, time, math, json, re, os
sys.path.insert(0, '/home/xilinx/claude_nlsg_20261110')
from sic_ctl import Sic
run, steps, dwell = sys.argv[1], sys.argv[2], float(sys.argv[3])
D = '/home/xilinx/claude_nlsg_20261110/sic/bidi/' + run
s = Sic(); out = []
def stat():
    try: l = [x for x in open(D + '/fdx.log', errors='ignore') if x.startswith('ONEWAY_STAT')][-1]
    except Exception: return {}
    return {k: int(v) for k, v in re.findall(r'(rx_good|rx_bad|rx_packets|tx_unique)=(\d+)', l)}
for st in steps:
    s.wr(0x08, int(st)); t0 = time.time(); a = stat(); pins = []; pouts = []
    while time.time() - t0 < dwell:
        last = s.rd(0x18) & 1
        while s.rd(0x18) & 1 == last: time.sleep(0.002)
        pin, pout, clips = s.monitor(); pins.append(pin); pouts.append(pout)
    b = stat(); pi = sum(pins) / len(pins); po = sum(pouts) / len(pouts)
    r = dict(sic=int(st), t=round(t0, 1), n=len(pins), pin_dB=round(10 * math.log10(pi), 2), pout_dB=round(10 * math.log10(po), 2), cancel_dB=round(10 * math.log10(pi / po), 2),
             cancel_min=round(min(10 * math.log10(x / y) for x, y in zip(pins, pouts)), 2), cancel_max=round(max(10 * math.log10(x / y) for x, y in zip(pins, pouts)), 2),
             d_good=b.get('rx_good', 0) - a.get('rx_good', 0), d_bad=b.get('rx_bad', 0) - a.get('rx_bad', 0), d_pk=b.get('rx_packets', 0) - a.get('rx_packets', 0))
    print(json.dumps(r), flush=True); out.append(r)
s.wr(0x08, 1)
json.dump(out, open(D + '/sic_ab.json', 'w'), indent=1)