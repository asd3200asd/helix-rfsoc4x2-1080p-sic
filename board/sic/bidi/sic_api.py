#!/usr/bin/env python3
"""SIC demo API (run as root on each board; port 18751).  Claude, 2026-11-15.
  GET /api/stats          JSON: SIC state, measured self-interference cancellation, powers, receiver/transmitter rates
  GET /api/sic?on=0|1     switch the SIC block (0xB0013000 reg 0x08)
  GET /                   the demo page (sic/bidi/demo.html)
Measurement (once a second, after the run's SIC calibration): capture 8192 samples of the transmit reference x and the ADC input y
(triggered by this board's own burst), fit the self-interference channel h_opt (8 taps; the captured reference is already delayed by the
block's delay line, so the lag is found near 0) by least squares, and compare
with the coefficients loaded in the SIC block h_sic (0 while it is off):
  SI       = |A h_opt|^2                 self-interference at the ADC
  remote   = |y - A h_opt|^2             the other board's signal + noise (what the receiver wants)
  residual = |A (h_opt - h_sic)|^2       self-interference left after the SIC block
  cancel_dB = 10 log10(SI / residual) (linear self-interference; non-linear parts count as 'remote'),  SIR before/after = remote / SI, remote / residual.
The monitor registers (power of the whole ADC signal before/after the block, ~17 ms windows) are reported as well.
SAFETY: reading a PL register while the bitstream is being (re)loaded hangs the CPU.  bidi_run.sh starts this service after the bit is
loaded and kills it before the next load; in addition it touches the PL only while fdx_bidi_claude is running and exits 15 s after it ends.
"""
import glob, json, math, os, re, subprocess, sys, threading, time
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
import numpy as np
W = '/home/xilinx/claude_nlsg_20261110'
sys.path.insert(0, W)
from sic_ctl import Sic
from sic_fit import find_delay
s = Sic(); lock = threading.Lock()
TAPS = s.taps
node = int(os.environ.get('HELIX_NODE', '0'))
state = dict(node=node, freq_mhz=1649.985 if node == 0 else 1650.0, sic=0, meas={}, link={}, run=None, history=[], t=0)
def run_dir():
    l = sorted(glob.glob('/dev/shm/bidi/*/fdx.log'), key=os.path.getmtime)
    return os.path.dirname(l[-1]) if l else None
def c(a): return a[:, 0] + 1j * a[:, 1]
def measure():
    with lock:
        en = s.rd(0x08) & 1; d0 = s.rd(0x10)
        hs = np.array([complex(s._s(s.rd(0x100 + 8 * k)), s._s(s.rd(0x104 + 8 * k))) for k in range(TAPS)]) / 32768 * en
        x, y = s.capture(0, 800, timeout=0.5); x, y = c(x), c(y)
        last = s.rd(0x18) & 1; t0 = time.time()
        while s.rd(0x18) & 1 == last and time.time() - t0 < 0.1: time.sleep(0.002)
        pin, pout, clips = s.monitor()
    # The captured reference already went through the block's delay line (reg 0x10), so the loaded taps apply at lag ~PRE=2.
    lag = find_delay(x, y, 64); d = max(lag - 2, 0)
    n = np.arange(d + TAPS + 8, len(x))
    A = np.stack([x[n - d - k] for k in range(TAPS)], axis=1)
    G = A.conj().T @ A; G += 1e-6 * np.trace(G).real / TAPS * np.eye(TAPS)
    ho = np.linalg.solve(G, A.conj().T @ y[n])
    si = np.mean(abs(A @ ho) ** 2); rem = np.mean(abs(y[n] - A @ ho) ** 2); res = np.mean(abs(A @ (ho - hs)) ** 2)
    db = lambda v: round(10 * math.log10(max(v, 1e-9)), 2)
    return dict(sic=int(en), si_dB=db(si), remote_dB=db(rem), residual_dB=db(res), cancel_dB=db(si / max(res, 1e-9)),
                sir_before_dB=db(rem / si), sir_after_dB=db(rem / max(res, 1e-9)),
                total_in_dB=db(pin), total_out_dB=db(pout), total_drop_dB=db(pin / max(pout, 1)), clips=int(clips),
                adc_peak=int(max(abs(y.real).max(), abs(y.imag).max())), lag=int(lag))
prev = {}
def link():
    global prev
    D = run_dir()
    if not D: return None, {}
    try: lines = [x for x in open(D + '/fdx.log', errors='ignore') if x.startswith('ONEWAY_STAT')]
    except Exception: return D, {}
    if not lines: return D, {}
    v = {k: float(x) for k, x in re.findall(r'(\w+)=([0-9.]+)', lines[-1])}
    out = dict(t_run=v.get('t'), rx_good=int(v.get('rx_good', 0)), rx_bad=int(v.get('rx_bad', 0)), rx_packets=int(v.get('rx_packets', 0)),
               tx_unique=int(v.get('tx_unique', 0)), tx_sent=int(v.get('tx_sent', 0)), rx_mbps=v.get('rx_mbps', 0), rx_self=int(v.get('rx_self', 0)))
    p = prev.get(D)
    if p and out['t_run'] != p['t_run']:
        dt = (out['t_run'] or 0) - (p['t_run'] or 0) or 5
        dp = out['rx_packets'] - p['rx_packets']
        out.update(rx_good_s=round((out['rx_good'] - p['rx_good']) / dt), rx_bad_s=round((out['rx_bad'] - p['rx_bad']) / dt),
                   bad_pct=round(100 * (out['rx_bad'] - p['rx_bad']) / dp, 1) if dp else 0, tx_mbps=round((out['tx_sent'] - p['tx_sent']) * 1337 * 8 / dt / 1e6, 2),
                   tx_unique_mbps=round((out['tx_unique'] - p['tx_unique']) * 1337 * 8 / dt / 1e6, 2))
        prev[D] = out
    elif p: out.update({k: p[k] for k in ('rx_good_s', 'rx_bad_s', 'bad_pct', 'tx_mbps', 'tx_unique_mbps') if k in p})
    else: prev[D] = out
    return D, out
def fdx_running(): return subprocess.run(['pgrep', '-f', 'build/[f]dx_bidi_claude'], capture_output=True).returncode == 0
alive = dict(ok=True, last=time.time())
def loop():
    while True:
        t = time.time(); D, l = link(); m = {}
        alive['ok'] = fdx_running()
        if alive['ok']: alive['last'] = t
        elif t - alive['last'] > 15: os._exit(0)
        if alive['ok']:
            try:
                cal = D and 'cal' in json.load(open(D + '/events.json'))
                if cal: m = measure()
            except BaseException as e: m = dict(error=str(e)[:80])
        else: m = dict(error='no run')
        with lock: state.update(run=D and os.path.basename(D), link=l, meas=m, sic=(s.rd(0x08) & 1) if alive['ok'] else state['sic'], t=round(t, 1))
        state['history'] = (state['history'] + [dict(t=round(t, 1), sic=state['sic'], cancel_dB=m.get('cancel_dB'), sir_after_dB=m.get('sir_after_dB'),
                            bad_pct=l.get('bad_pct'), rx_good_s=l.get('rx_good_s'))])[-180:]
        time.sleep(max(0.2, 1.0 - (time.time() - t)))
class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype='application/json'):
        b = body if isinstance(body, bytes) else body.encode()
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Cache-Control', 'no-store'); self.send_header('Content-Length', str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_GET(self):
        if self.path.startswith('/api/stats'):
            with lock: return self.send(200, json.dumps(state))
        if self.path.startswith('/api/sic'):
            m = re.search(r'on=([01])', self.path)
            if not m: return self.send(400, '{"error":"on=0|1"}')
            if not alive['ok']: return self.send(409, '{"error":"no run"}')
            with lock: s.wr(0x08, int(m.group(1))); state['sic'] = s.rd(0x08) & 1
            ev = dict(t=round(time.time(), 1), set_sic=int(m.group(1)))
            try: open(state['run'] and f"/dev/shm/bidi/{state['run']}/sic_api_events.log" or '/tmp/sic_api_events.log', 'a').write(json.dumps(ev) + '\n')
            except Exception: pass
            return self.send(200, json.dumps(dict(sic=state['sic'])))
        if self.path in ('/', '/demo', '/demo.html'):
            try: return self.send(200, open(W + '/sic/bidi/demo.html', 'rb').read(), 'text/html; charset=utf-8')
            except Exception as e: return self.send(404, str(e), 'text/plain')
        self.send(404, '{}')
threading.Thread(target=loop, daemon=True).start()
ThreadingHTTPServer(('0.0.0.0', int(os.environ.get('SIC_API_PORT', '18751'))), H).serve_forever()