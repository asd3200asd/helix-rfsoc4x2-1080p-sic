"""SIC timeline for the full-duplex test (run as root while fdx_sic_claude runs as a normal user).
usage: orchestrate.py <outdir> <fdx_log> <cal_at_s> <schedule 't:state,...'> <end_s> [taps=8]
Waits for ONEWAY_READY in the fdx log, calibrates once at cal_at (capture -> LS fit -> load, SIC still off),
then switches the SIC on/off at the scheduled times and records monotonic timestamps for every event."""
import sys, json, time, os, numpy as np
sys.path.insert(0, '/home/xilinx/claude_nlsg_20261110')
from sic_ctl import Sic
from sic_model import quantize
from sic_fit import find_delay
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
    t, st = item.split(':'); t = float(t); st = int(st)
    wait_until(t); s.wr(0x08, st); ev['events'].append(dict(t_s=t, t_ns=time.monotonic_ns(), sic=st))
    if st and ev['cal'] and 'error' not in ev['cal']:
        time.sleep(0.4)
        try:
            d, e = s.capture(1, 800, timeout=2.0); d, e = c(d), c(e)
            pd, pe = float(np.mean(abs(d[600:]) ** 2)), float(np.mean(abs(e[600:]) ** 2))
            ev['onchecks'].append(dict(t_s=t, rx_power_before=pd, rx_power_after=pe, cancel_dB=float(10 * np.log10(pd / pe))))
        except SystemExit as ex:
            ev['onchecks'].append(dict(t_s=t, error=str(ex)))
    save()
wait_until(end_s); s.wr(0x08, 0); ev['events'].append(dict(t_s=end_s, t_ns=time.monotonic_ns(), sic=0)); save()
print('ORCH_DONE')
