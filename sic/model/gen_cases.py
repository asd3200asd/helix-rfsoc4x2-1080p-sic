"""Generate stimulus and expected output for every test case under ../cases/<name>/."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sic_model import sic, quantize, REF_CONST, CMAX, CMIN, latency
from sic_fit import fit

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'cases')
rng = np.random.default_rng(20261111)


def rand_iq(n, amp):
    return rng.integers(-amp, amp + 1, size=(n, 2), dtype=np.int64)


def write_case(name, taps, delay, y, x, sched, en, note, extra=None):
    """sched: list of (beat, coef int array (taps,2)); set is committed at `beat`."""
    nb = len(y) // 2
    rows = np.zeros((nb, 14), dtype=np.int64)
    rows[:, 0:2] = y[0::2]; rows[:, 2:4] = y[1::2]
    rows[:, 4:6] = x[0::2]; rows[:, 6:8] = x[1::2]
    rows[:, 8] = en
    h_beat = np.zeros((nb, taps, 2), dtype=np.int64)
    for beat, coef in sched:
        assert beat >= taps, 'need room for shadow writes before commit'
        for k in range(taps):
            r = beat - taps + k
            assert rows[r, 10] == 0, 'overlapping coefficient writes'
            rows[r, 10] = 1; rows[r, 11] = k; rows[r, 12] = coef[k, 0]; rows[r, 13] = coef[k, 1]
        rows[beat, 9] = 1
        h_beat[beat:] = coef
    e, clip, est = sic(y, x, h_beat, en, delay)
    d = os.path.join(ROOT, name); os.makedirs(d, exist_ok=True)
    np.savetxt(os.path.join(d, 'stim.txt'), rows, fmt='%d')
    exp = np.concatenate([e[0::2], e[1::2], y[0::2], y[1::2],
                          (clip[0::2, 0] | clip[0::2, 1] << 1 | clip[1::2, 0] << 2 | clip[1::2, 1] << 3)[:, None]], axis=1)
    np.savetxt(os.path.join(d, 'exp.txt'), exp, fmt='%d')
    meta = dict(name=name, taps=taps, delay=int(delay), beats=nb, latency=latency(taps),
                commits=len(sched), note=note, clipped_samples=int(clip.any(axis=1).sum()))
    if extra: meta.update(extra)
    json.dump(meta, open(os.path.join(d, 'meta.json'), 'w'), indent=1, ensure_ascii=False)
    return e, est, meta


def rand_coef(taps, amp):
    return rng.integers(-amp, amp + 1, size=(taps, 2), dtype=np.int64)


def db(a, b):
    return 10 * np.log10(np.sum(a.astype(np.float64) ** 2) / max(np.sum(b.astype(np.float64) ** 2), 1e-30))


def bandlimited(n, occupied, rms):
    f = np.fft.fftfreq(n)
    s = (rng.choice([-1, 1], n) + 1j * rng.choice([-1, 1], n)) * (np.abs(f) < occupied / 2)
    t = np.fft.ifft(s); t *= rms / np.sqrt(np.mean(np.abs(t) ** 2))
    return t


def to_int(c):
    return np.stack([np.round(c.real), np.round(c.imag)], axis=1).astype(np.int64)


def frac_delay(sig, d):
    return np.fft.ifft(np.fft.fft(sig) * np.exp(-2j * np.pi * np.fft.fftfreq(len(sig)) * d))


NB = 4000; N = 2 * NB
summary = []

# 1. bypass: random coefficients loaded but enable = 0 -> output identical to input
y = rand_iq(N, 32767); x = rand_iq(N, 32767)
e, _, m = write_case('c1_bypass', 4, 37, y, x, [(8, rand_coef(4, 60000))], np.zeros(NB, int),
                     'enable=0 with non-zero coefficients; output must equal input')
assert np.array_equal(e, y); summary.append(m)

# 2. enabled with all-zero coefficients
y = rand_iq(N, 32767); x = rand_iq(N, 32767)
e, _, m = write_case('c2_zero_coef', 4, 37, y, x, [(8, np.zeros((4, 2), np.int64))], np.ones(NB, int),
                     'enable=1, h=0; output must equal input')
assert np.array_equal(e, y); summary.append(m)

# 3. local TX only, channel exactly representable -> residual exactly zero after the commit
g = quantize(np.array([0.05 - 0.02j, 0.41 + 0.33j, -0.12 + 0.08j, 0.03 + 0.01j]))
x = rand_iq(N, 20000)
hb = np.broadcast_to(g, (NB, 4, 2)).copy()
_, _, si = sic(np.zeros((N, 2), np.int64), x, hb, np.ones(NB, int), 301)
e, _, m = write_case('c3_si_only', 4, 301, si, x, [(8, g)], np.ones(NB, int),
                     'y = quantized SI only (odd delay); residual must be exactly 0 after commit')
assert not e[2 * 8:].any(); summary.append(m)

# 4. SI + independent desired signal -> output equals the desired signal exactly
dsig = rand_iq(N, 3000)
e, _, m = write_case('c4_si_plus_desired', 4, 301, si + dsig, x, [(8, g)], np.ones(NB, int),
                     'y = SI + independent remote signal; output must equal the remote signal')
assert np.array_equal(e[2 * 8:], dsig[2 * 8:]); summary.append(m)

# 5-8. random full-scale data with extreme values, several TAPS and delays
for name, taps, delay in (('c5_random_t4', 4, 255), ('c6_random_t8', 8, 1024),
                          ('c7_random_t1', 1, 0), ('c8_random_t16', 16, 4093)):
    y = rand_iq(N, 32767); x = rand_iq(N, 32767)
    for arr in (y, x):
        pos = rng.integers(0, N, 200)
        arr[pos] = rng.choice([-32768, 32767], size=(200, 2))
    coef = rand_coef(taps, 40000); coef[0] = [CMIN, CMAX]
    if taps > 1: coef[-1] = [CMAX, CMIN]
    e, _, m = write_case(name, taps, delay, y, x, [(taps + 4, coef)], np.ones(NB, int),
                         'random full-scale samples and extreme coefficients (saturation paths)')
    summary.append(m)

# 9. coefficient swaps and enable toggles in mid-stream
y = rand_iq(N, 30000); x = rand_iq(N, 30000)
en = np.ones(NB, int); en[:20] = 0; en[1500:1503] = 0; en[2600:3100] = 0; en[3999] = 0
sched = [(40, rand_coef(4, 20000)), (1000, rand_coef(4, 20000)), (1001 + 4, rand_coef(4, 20000)),
         (2800, rand_coef(4, 20000)), (3500, np.zeros((4, 2), np.int64))]
e, _, m = write_case('c9_switch', 4, 64, y, x, sched, en,
                     '5 atomic coefficient commits and 4 enable changes while samples flow')
summary.append(m)

# 10. realistic float channel: fit on a TX-only segment, verify on a held-out segment with a remote signal
NB2 = 16000; N2 = 2 * NB2
xt = bandlimited(N2, 0.43, 6000.0)
ch = 0.50 * np.exp(1j * 0.7) * frac_delay(xt, 145.3) + 0.12 * np.exp(-1j * 1.9) * frac_delay(xt, 147.8)
remote = bandlimited(N2, 0.43, 6000.0 * 0.5 * 10 ** (-30 / 20))
remote[:N2 // 2] = 0                       # remote silent during training
noise = (rng.standard_normal(N2) + 1j * rng.standard_normal(N2)) * 2.0
xi = to_int(xt); yi = to_int(ch + remote + noise); si_i = to_int(ch); rem_i = to_int(remote)
res = {}
for taps in (2, 4, 8, 16):
    tr = slice(2000, N2 // 2 - 2000)
    dly, hf, hq = fit(xi[tr, 0] + 1j * xi[tr, 1], yi[tr, 0] + 1j * yi[tr, 1], taps=taps, pre=min(1, taps - 1))
    commit = taps + 4
    if taps in (4, 8):
        e, est, m = write_case(f'c10_fit_t{taps}', taps, dly, yi, xi, [(commit, hq)], np.ones(NB2, int),
                               'float 2-path channel with fractional delays; LS fit on TX-only half, '
                               'held-out half carries a remote signal 30 dB below the SI')
    else:
        hb = np.broadcast_to(hq, (NB2, taps, 2)).copy()
        e, _, est = sic(yi, xi, hb, np.ones(NB2, int), dly)
    ho = slice(N2 // 2 + 2000, N2 - 2000)
    r = dict(taps=taps, cfg_delay=int(dly),
             heldout_si_cancel_dB=round(db(si_i[ho], (e - rem_i)[ho]), 2),
             remote_error_dB=round(db(rem_i[ho], (e - rem_i)[ho]), 2),
             coef_q15=hq.tolist())
    res[taps] = r
    if taps in (4, 8):
        m.update(r); json.dump(m, open(os.path.join(ROOT, f'c10_fit_t{taps}', 'meta.json'), 'w'), indent=1, ensure_ascii=False)
        summary.append(m)
json.dump(dict(cases=summary, fit_sweep=res), open(os.path.join(ROOT, 'summary.json'), 'w'), indent=1, ensure_ascii=False)
for m in summary: print(m['name'], 'taps', m['taps'], 'delay', m['delay'], 'beats', m['beats'], 'clipped', m['clipped_samples'])
for t, r in res.items(): print('fit taps', t, 'cfg_delay', r['cfg_delay'], 'held-out SI cancel dB', r['heldout_si_cancel_dB'], 'remote SNR after SIC dB', r['remote_error_dB'])
