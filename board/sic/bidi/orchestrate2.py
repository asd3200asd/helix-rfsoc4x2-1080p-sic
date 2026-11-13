"""SIC timeline for the full-duplex test (run as root while fdx_sic_claude runs as a normal user).
usage: orchestrate2.py (2026-11-12: schedule items may be t:sic:agc, agc = k | off | <Vref>; AGC restored to the library default at the end)
usage: orchestrate.py <outdir> <fdx_log> <cal_at_s> <schedule 't:state,...'> <end_s> [taps=8]
Waits for ONEWAY_READY in the fdx log, calibrates once at cal_at (capture -> LS fit -> load, SIC still off),
then switches the SIC on/off at the scheduled times and records monotonic timestamps for every event."""
import sys, json, time, os, numpy as np
sys.path.insert(0, '/home/xilinx/claude_nlsg_20261110')
from sic_ctl import Sic
from sic_model import quantize
from sic_fit import find_delay
import mmap as _mm
_fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC); _agc = memoryview(_mm.mmap(_fd, 0x1000, offset=0xB0016000)).cast('I')   # receive AGC: Vref | enable<<31 (library default 800 | 1<<31)
def agc_set(tok):   # 'k' keep, 'off' = gain 1, <n> = AGC on with Vref n (>=2400 pegs the gain on the PC-side board)
    if tok == 'k': return None
    v = 800 if tok == 'off' else ((int(tok) & 0x7FFF) | (1 << 31)); _agc[0] = v; return v
out, flog, cal_at, sched, end_s = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4], float(sys.argv[5])
TAPS = int(sys.argv[6]) if len(sys.argv) > 6 else 8; PRE = 2
c = lambda a: a[:, 0] + 1j * a[:, 1]
s = Sic(); s.wr(0x08, 0); s.wr(0x10, 0); s.set_coef([])
ev = dict(events=[], cal=None, onchecks=[])
def save(): json.dump(ev, open(os.path.join(out, 'events.json'), 'w'), indent=1)
for _ in range(600):
    if os.path.exists(flog) and 'ONEWAY_READY' in open(flog, errors='replace').read(): break
    time.sleep(0.05)
else: raise SystemExit('fdx never became ready')
T0 = time.monotonic_ns(); ev['t0_ns'] = T0
if os.environ.get('HOLDOFF'):   # slot hold-off of helix_nlsg_sic2.bit: SSB R1[31:17] = beats/4 (0 = built-in 40550 beats = 165 us)
    _ssb = memoryview(_mm.mmap(_fd, 0x1000, offset=0xB0009000)).cast('I'); _r1 = _ssb[1]
    _ssb[1] = (_r1 & 0x1FFFF) | ((int(os.environ['HOLDOFF']) // 4) << 17); ev['ssb_r1'] = [int(_r1), int(_ssb[1])]
def wait_until(t):
    while time.monotonic_ns() < T0 + int(t * 1e9): time.sleep(0.002)
wait_until(cal_at)
try:
    x, y = s.capture(0, 800, timeout=5.0); x, y = c(x), c(y)
    lag = find_delay(x, y, 2048); d0 = lag - PRE
    n = np.arange(d0 + TAPS + 8, len(x))
    A = np.stack([x[n - d0 - k] for k in range(TAPS)], axis=1)
    G = A.conj().T @ A; G += 1e-6 * np.trace(G).real / TAPS * np.eye(TAPS)
    h = np.linalg.solve(G, A.conj().T @ y[n]); hq = quantize(h)
    res = y[n] - A @ ((hq[:, 0] + 1j * hq[:, 1]) / 32768)
    s.wr(0x10, d0); s.set_coef([tuple(int(v) for v in r) for r in hq])
    ev['cal'] = dict(t_ns=time.monotonic_ns(), lag=int(lag), cfg_delay=int(d0), coef_q15=hq.tolist(),
                     ref_rms=float(np.sqrt(np.mean(abs(x[n]) ** 2))), si_power=float(np.mean(abs(y[n]) ** 2)),
                     residual_power=float(np.mean(abs(res) ** 2)), predicted_dB=float(10 * np.log10(np.mean(abs(y[n]) ** 2) / np.mean(abs(res) ** 2))),
                     adc_peak=int(max(abs(y.real).max(), abs(y.imag).max())))
    np.savez(os.path.join(out, 'cal_capture.npz'), x=x, y=y)
except SystemExit as e:
    ev['cal'] = dict(error=str(e))
save()
for item in sched.split(','):
    f = item.split(':'); t = float(f[0]); st = int(f[1]); ag = f[2] if len(f) > 2 else 'k'
    wait_until(t); av = agc_set(ag); s.wr(0x08, st); ev['events'].append(dict(t_s=t, t_ns=time.monotonic_ns(), sic=st, agc=ag, agc_reg=av))
    time.sleep(0.4); caps = []
    for k in range(5):                      # raw captures for offline in-band analysis: SIC on -> (RX delayed, SIC out); off -> (reference, RX in)
        try: p, q = s.capture(1 if st else 0, 800, timeout=0.25); trig = 1
        except SystemExit:
            try: p, q = s.capture(1 if st else 0, 0, timeout=0.5); trig = 0
            except SystemExit: continue
        caps.append((c(p), c(q), trig, time.monotonic_ns())); time.sleep(0.15)
    if caps: np.savez(os.path.join(out, 'caps_%05.1f_%d.npz' % (t, st)), a=np.array([k[0] for k in caps]), b=np.array([k[1] for k in caps]), trig=np.array([k[2] for k in caps]), t_ns=np.array([k[3] for k in caps]))
    save()
wait_until(end_s); _agc[0] = 800 | (1 << 31); s.wr(0x08, 0); ev['events'].append(dict(t_s=end_s, t_ns=time.monotonic_ns(), sic=0)); save()
print('ORCH_DONE')
