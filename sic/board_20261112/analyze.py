"""Attribute every received packet to the local or the remote stream and summarise per SIC phase.
usage: analyze.py <rundir> <local_file> [remote_file]"""
import sys, json, os, zlib, numpy as np
run, lf = sys.argv[1], sys.argv[2]; rf = sys.argv[3] if len(sys.argv) > 3 else None
PAY = 1337
def crcs(f):
    b = open(f, 'rb').read(); n = (len(b) + PAY - 1) // PAY
    return np.array([zlib.crc32(b[i * PAY:(i + 1) * PAY]) & 0xFFFFFFFF for i in range(n)], dtype=np.uint64)
cl = crcs(lf); cr = crcs(rf) if rf else np.zeros(0, np.uint64)
rec = np.fromfile(os.path.join(run, 'pkt.bin'), dtype=np.dtype([('t', '<u8'), ('ok', 'u1'), ('z', 'u1'), ('sq', '<u2'), ('c', '<u4')]))
ev = json.load(open(os.path.join(run, 'events.json')))
sq = rec['sq'].astype(np.int64); c = rec['c'].astype(np.uint64); ok = rec['ok'] == 1
isl = ok & (sq < len(cl)) & (cl[np.minimum(sq, len(cl) - 1)] == c)
isr = ok & (sq < len(cr)) & (cr[np.minimum(sq, max(len(cr) - 1, 0))] == c) if len(cr) else np.zeros(len(rec), bool)
t = (rec['t'].astype(np.int64) - ev['t0_ns']) / 1e9
out = dict(cal=ev.get('cal'), onchecks=ev.get('onchecks'), packets=int(len(rec)), phases=[])
edges = [(0.0, 0)] + [((e['t_ns'] - ev['t0_ns']) / 1e9, e['sic']) for e in ev['events']]
tend = float(t.max()) if len(t) else 0.0
for i, (ts, st) in enumerate(edges):
    te = edges[i + 1][0] if i + 1 < len(edges) else tend
    a, b = ts + 0.3, te - 0.05
    if b - a < 0.5: continue
    m = (t >= a) & (t < b); dur = b - a
    ru = np.unique(sq[m & isr]); span = int(ru.max() - ru.min() + 1) if len(ru) else 0
    out['phases'].append(dict(t0=round(a, 2), t1=round(b, 2), sic=st, pkts_per_s=round(float(m.sum() / dur), 1),
        local_good_per_s=round(float((m & isl).sum() / dur), 1), remote_good_per_s=round(float((m & isr).sum() / dur), 1),
        other_good=int((m & ok & ~isl & ~isr).sum()), bad_per_s=round(float((m & ~ok).sum() / dur), 1),
        remote_unique=int(len(ru)), remote_seq_span=span, remote_unique_recovered_pct=round(100.0 * len(ru) / span, 2) if span else None))
json.dump(out, open(os.path.join(run, 'summary.json'), 'w'), indent=1)
if out['cal']: print('CAL', {k: (round(v, 2) if isinstance(v, float) else v) for k, v in out['cal'].items() if k != 'coef_q15'})
for o in out['onchecks'] or []: print('ONCHECK', {k: (round(v, 2) if isinstance(v, float) else v) for k, v in o.items()})
for p in out['phases']: print('PHASE', p)
