"""Claude 2026-11-12: where inside a burst do the bad packets of a live (bursty) transmission fall?
usage: burstpos.py <pktlog (12-byte records: u64 t_ns, u8 good, u8 0, u16 seq)> [gap_ms=3]"""
import sys, numpy as np
r = np.fromfile(sys.argv[1], dtype=np.dtype([('t', '<u8'), ('ok', 'u1'), ('z', 'u1'), ('sq', '<u2')])); gap = (float(sys.argv[2]) if len(sys.argv) > 2 else 3) * 1e6
t = r['t'].astype(np.int64); bad = r['ok'] == 0; dt = np.diff(t)
print('packets', len(r), 'bad', int(bad.sum()), 'bad%% %.3f' % (100 * bad.mean()))
print('inter-packet time percentiles ms (50/90/99/99.9):', np.round(np.percentile(dt, [50, 90, 99, 99.9]) / 1e6, 3))
st = np.r_[0, np.where(dt > gap)[0] + 1]; en = np.r_[st[1:], len(r)]; L = en - st
pos = np.arange(len(r)) - np.repeat(st, L); rpos = np.repeat(L, L) - 1 - pos
print('bursts', len(st), 'burst length percentiles (10/50/90):', np.percentile(L, [10, 50, 90]))
for name, m in (('first packet of burst', pos == 0), ('2nd-4th', (pos >= 1) & (pos <= 3)), ('5th and later, not last', (pos >= 4) & (rpos >= 1)), ('last packet of burst', (rpos == 0) & (pos > 0))):
    print('%-26s n=%7d bad=%5d  bad%%=%.3f' % (name, m.sum(), (bad & m).sum(), 100 * (bad & m).sum() / max(1, m.sum())))
prev = np.r_[np.inf, dt]; 
for lo, hi in ((0, 0.3), (0.3, 1), (1, 3), (3, 10), (10, 1e9)):
    m = (prev >= lo * 1e6) & (prev < hi * 1e6); print('gap before packet %5.1f-%-6.1f ms n=%7d bad%%=%.3f' % (lo, min(hi, 9999), m.sum(), 100 * (bad & m).sum() / max(1, m.sum())))
b = np.where(bad)[0]; print('isolated bad (no bad neighbour): %d of %d' % (int(((np.diff(np.r_[-9, b]) > 1) & (np.diff(np.r_[b, len(r) + 9]) > 1)).sum()), len(b)))
