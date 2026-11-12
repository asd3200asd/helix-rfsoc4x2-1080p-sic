import sys, numpy as np
sys.path.insert(0, '/home/xilinx/claude_nlsg_20261110')
from sic_fit import find_delay
z = np.load(sys.argv[1]); X, Y, T = z['x'], z['y'], z['t']; FS = 491.52; TAPS, PRE = 8, 2
f = np.fft.fftshift(np.fft.fftfreq(512, 1 / FS)); inb = abs(f) < 100
def psd(v, N=512):
    v = v[:len(v) // N * N].reshape(-1, N) * np.hanning(N); return np.fft.fftshift(np.mean(abs(np.fft.fft(v, axis=1)) ** 2, axis=0)) / N
def inband(v): return psd(v)[inb].sum()
lag = find_delay(X[0], Y[0], 2048); d0 = lag - PRE; n = np.arange(d0 + TAPS + 8, X.shape[1])
def A(x): return np.stack([x[n - d0 - k] for k in range(TAPS)], axis=1)
w0 = np.linalg.lstsq(A(X[0]), Y[0][n], rcond=None)[0]
print('lag', lag, 'captures', len(T))
rows = []
for i in range(len(T)):
    if find_delay(X[i], Y[i], 2048) != lag: rows.append((T[i], None, None, None)); continue
    wi = np.linalg.lstsq(A(X[i]), Y[i][n], rcond=None)[0]
    py = inband(Y[i][n]); r0 = inband(Y[i][n] - A(X[i]) @ w0); ri = inband(Y[i][n] - A(X[i]) @ wi)
    g = np.vdot(w0, wi) / np.vdot(w0, w0)
    rows.append((T[i], 10 * np.log10(py / r0), 10 * np.log10(py / ri), np.degrees(np.angle(g)), abs(g)))
for r in rows[:: max(1, len(rows) // 24)]:
    print('t=%5.1fs' % r[0], 'lag changed' if r[1] is None else 'fixed-coef cancel %5.1f dB | refit %5.1f dB | phase vs t0 %6.1f deg | gain %.3f' % r[1:])
