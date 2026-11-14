import numpy as np
from scipy.io import loadmat
q = loadmat(r'C:/Users/asd32/Documents/Codex/RFSoC/helix_original/matlab/decoding_functions/SSB_DECODING/PSS_filter_coeff.mat')['PSS_filter_coeff']
s = np.loadtxt('stimulus.txt', dtype=int); x = s[:, 0] + 1j * s[:, 1]
nz = np.flatnonzero(np.abs(x) > 0)
print('stimulus', len(x), 'max', np.abs(x).max(), 'nonzero span', nz[0], nz[-1], 'flags', np.unique(s[:, 2]))
for c in range(3):
    y = np.convolve(x, q[:, c]); k = np.argmax(abs(y)); print('corr with q%d peak %.3g at %d' % (c, abs(y[k]), k))
t = [np.conj(q[::-1, c]) for c in range(3)]            # matched-filter templates
y0 = np.convolve(x, q[:, 0]); k = int(np.argmax(abs(y0))); st = k - 255
seg = x[st:st + 256]; a = np.vdot(t[0], seg) / np.vdot(t[0], t[0]); print('PSS0 at', st, 'scale', abs(a))
rng = np.random.default_rng(1); A = abs(a); N = 9000; sig = 0.04 * np.abs(seg).std()
def mk(name, items):
    z = (rng.normal(size=N) + 1j * rng.normal(size=N)) * sig
    for (c, pos, amp) in items: z[pos:pos + 256] += a / abs(a) * amp * A * t[c]
    m = np.abs(np.r_[z.real, z.imag]).max()
    zi = np.clip(np.rint(z.real), -32768, 32767).astype(int); zq = np.clip(np.rint(z.imag), -32768, 32767).astype(int)
    np.savetxt(name, np.c_[zi, zq, np.zeros(N, int)], fmt='%d'); print(name, 'max', m, 'bursts', items)
mk('stim_p0.txt', [(0, 1000, 1), (0, 3500, 1), (0, 6000, 1)])
mk('stim_p1.txt', [(1, 1000, 1), (1, 3500, 1), (1, 6000, 1)])
mk('stim_mix.txt', [(0, 1000, 1), (0, 3500, 1), (0, 6000, 1), (1, 2200, 0.32), (1, 4700, 0.32), (1, 7200, 0.32)])   # own PSS0 + remote PSS1 10 dB weaker