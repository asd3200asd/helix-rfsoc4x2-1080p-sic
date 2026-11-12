"""Capture reference/RX pairs repeatedly (SIC off) to see how long one set of coefficients stays valid.
usage: drift_probe.py <out.npz> <seconds>"""
import sys, time, numpy as np
sys.path.insert(0, '/home/xilinx/claude_nlsg_20261110')
from sic_ctl import Sic
s = Sic(); s.wr(0x08, 0); s.wr(0x10, 0)
X, Y, T = [], [], []; t0 = time.monotonic()
while time.monotonic() - t0 < float(sys.argv[2]):
    try: a, b = s.capture(0, 800, timeout=1.0)
    except SystemExit: continue
    X.append(a[:, 0] + 1j * a[:, 1]); Y.append(b[:, 0] + 1j * b[:, 1]); T.append(time.monotonic() - t0)
np.savez(sys.argv[1], x=np.array(X), y=np.array(Y), t=np.array(T)); print('captures', len(T))
