import sys, json, numpy as np
sys.path.insert(0, '..')
from sic_model import sic, quantize, REF_CONST
from sic_fit import find_delay
def load(f):
    z = np.load(f); return z['a'], z['b']
def c(a): return a[:, 0] + 1j * a[:, 1]
def db(p, q): return 10 * np.log10(p / q)
xt, yt = load(sys.argv[1]); held = [load(f) for f in sys.argv[2:]]
noise = float(np.mean(np.abs(c(load('idle0.npz')[1])) ** 2))
lag = find_delay(c(xt), c(yt), 2048)
print('lag(samples, captured ref -> RX) =', lag, ' noise power', round(noise, 1), ' RX power', round(float(np.mean(np.abs(c(yt)[lag+50:])**2)), 1))
out = {}
for taps in (1, 2, 4, 8):
    pre = min(2, taps - 1) if taps > 2 else (taps - 1)
    d0 = lag - pre
    n = np.arange(d0 + taps + 8, len(xt))
    A = np.stack([c(xt)[n - d0 - k] for k in range(taps)], axis=1)
    G = A.conj().T @ A; G += 1e-6 * np.trace(G).real / taps * np.eye(taps)
    h = np.linalg.solve(G, A.conj().T @ c(yt)[n]); hq = quantize(h)
    res = []
    for (x, y) in [(xt, yt)] + held:
        hb = np.broadcast_to(hq, (len(y) // 2, taps, 2)).copy()
        e, clip, est = sic(y, x, hb, np.ones(len(y) // 2, int), d0 - REF_CONST)
        s = slice(lag + 64, len(y))
        res.append(round(float(db(np.mean(np.abs(c(y)[s]) ** 2), np.mean(np.abs(c(e)[s]) ** 2))), 2))
    out[taps] = dict(cfg_delay=int(d0), coef=hq.tolist(), cancel_dB_train=res[0], cancel_dB_heldout=res[1:])
    print('taps', taps, 'cfg_delay', d0, 'cancel dB train', res[0], 'held-out', res[1:], '|h| max', round(float(np.abs(h).max()), 4))
json.dump(out, open('fit_result.json', 'w'), indent=1)
