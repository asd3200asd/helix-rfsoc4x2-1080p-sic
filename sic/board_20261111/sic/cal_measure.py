"""Within one host run: capture -> LS fit -> load -> enable -> capture before/after -> disable."""
import sys, json, time, numpy as np
sys.path.insert(0, '..')
from sic_ctl import Sic
from sic_model import quantize
from sic_fit import find_delay
c = lambda a: a[:, 0] + 1j * a[:, 1]
TAPS, PRE = 8, 2
s = Sic(); s.wr(0x08, 0); s.wr(0x10, 0)
x, y = s.capture(0, 800); x, y = c(x), c(y)
lag = find_delay(x, y, 2048); d0 = lag - PRE
n = np.arange(d0 + TAPS + 8, len(x))
A = np.stack([x[n - d0 - k] for k in range(TAPS)], axis=1)
G = A.conj().T @ A; G += 1e-6 * np.trace(G).real / TAPS * np.eye(TAPS)
h = np.linalg.solve(G, A.conj().T @ y[n]); hq = quantize(h)
pred = 10 * np.log10(np.mean(abs(y[n]) ** 2) / np.mean(abs(y[n] - A @ ((hq[:, 0] + 1j * hq[:, 1]) / 32768)) ** 2))
s.wr(0x10, d0); s.set_coef([tuple(int(v) for v in r) for r in hq])
x2, y2 = s.capture(0, 800); lag2 = find_delay(c(x2), c(y2), 2048)
s.wr(0x08, 1)
d, e = s.capture(1, 800)
s.wr(0x08, 0)
d, e = c(d), c(e); sl = slice(2 * (lag + 64), None) if False else slice(600, None)
pd, pe = np.mean(abs(d[sl]) ** 2), np.mean(abs(e[sl]) ** 2)
res = dict(lag=int(lag), cfg_delay=int(d0), lag_after_align=int(lag2), predicted_dB=round(float(pred), 2),
           rx_power_before=round(float(pd), 1), rx_power_after=round(float(pe), 1),
           measured_cancel_dB=round(float(10 * np.log10(pd / pe)), 2), coef_q15=hq.tolist(), time=time.strftime('%H:%M:%S'))
print(json.dumps(res)); np.savez(sys.argv[1], d=d, e=e, x=x, y=y); json.dump(res, open(sys.argv[1] + '.json', 'w'))
