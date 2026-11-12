"""Claude 2026-11-12: per-interval timeline of a full-duplex run.  usage: tl.py <run> <t_from> <t_to> <step>"""
import sys, zlib, numpy as np, json
run, a0, a1, st = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4]); PAY = 1337
def crcs(f):
    b = open(f, 'rb').read(); n = (len(b) + PAY - 1) // PAY
    return np.array([zlib.crc32(b[i * PAY:(i + 1) * PAY]) & 0xFFFFFFFF for i in range(n)], dtype=np.uint64)
cl, cr = crcs('local_20000tb.bin'), crcs('remote_50000tb.bin')
rec = np.fromfile(run + '/pkt.bin', dtype=np.dtype([('t', '<u8'), ('ok', 'u1'), ('z', 'u1'), ('sq', '<u2'), ('c', '<u4')])); ev = json.load(open(run + '/events.json'))
t = (rec['t'].astype(np.int64) - ev['t0_ns']) / 1e9; sq = rec['sq'].astype(np.int64); c = rec['c'].astype(np.uint64); ok = rec['ok'] == 1
isl = ok & (sq < len(cl)) & (cl[np.minimum(sq, len(cl) - 1)] == c); isr = ok & (sq < len(cr)) & (cr[np.minimum(sq, len(cr) - 1)] == c)
print('cal', {k: v for k, v in (ev.get('cal') or {}).items() if k in ('error', 'lag', 'predicted_dB', 'adc_peak')}, 'events', [(round((e['t_ns'] - ev['t0_ns']) / 1e9, 2), e['sic'], e.get('agc')) for e in ev['events']], 'last_pkt %.2f' % (t.max() if len(t) else -1))
for a in np.arange(a0, a1, st):
    m = (t >= a) & (t < a + st); print('%6.1f pk/s=%6.0f local_good/s=%6.1f remote_good/s=%6.0f bad/s=%6.0f' % (a, m.sum() / st, (m & isl).sum() / st, (m & isr).sum() / st, (m & ~ok).sum() / st))
