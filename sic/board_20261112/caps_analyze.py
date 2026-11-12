"""In-band (|f|<100 MHz) power of every raw capture of a run, and the SI cancellation the loaded coefficients give.
usage: caps_analyze.py <rundir>"""
import sys, os, glob, json, numpy as np
run = sys.argv[1]; FS = 491.52
f = np.fft.fftshift(np.fft.fftfreq(512, 1 / FS)); inb = abs(f) < 100
def psd(v, N=512):
    v = v[:len(v) // N * N].reshape(-1, N) * np.hanning(N); return np.fft.fftshift(np.mean(abs(np.fft.fft(v, axis=1)) ** 2, axis=0)) / N
dB = lambda v: round(float(10 * np.log10(psd(v)[inb].sum() + 1e-9)), 1)
ev = json.load(open(os.path.join(run, 'events.json'))); cal = ev['cal']
w = None
if cal and 'coef_q15' in cal:
    h = np.array(cal['coef_q15']); w = (h[:, 0] + 1j * h[:, 1]) / 32768; d0 = cal['cfg_delay']
for fn in sorted(glob.glob(os.path.join(run, 'caps_*.npz'))):
    z = np.load(fn); st = int(fn[-5]); out = []
    for a, b, trig in zip(z['a'], z['b'], z['trig']):
        if st:   # a = RX delayed, b = SIC output
            out.append('before %.1f after %.1f (%.1f dB)' % (dB(a[600:]), dB(b[600:]), dB(a[600:]) - dB(b[600:])))
        else:    # a = reference (delay register applied), b = RX input
            if trig and w is not None and np.mean(abs(a) ** 2) > 1e4:
                n = np.arange(32, len(a)); A = np.stack([a[n - k] for k in range(8)], axis=1)   # reference is already delayed by cfg_delay in hardware
                r = b[n] - A @ w; out.append('RX %.1f, model residual %.1f (%.1f dB)' % (dB(b[n]), dB(r), dB(b[n]) - dB(r)))
            else: out.append('RX %.1f (local silent)' % dB(b))
    print(os.path.basename(fn), 'SIC', 'ON ' if st else 'off', '|', ' ; '.join(out))
