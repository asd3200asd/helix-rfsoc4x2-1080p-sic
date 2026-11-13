"""Claude 2026-11-13: received.h264 vs remote_source.h264 in one run directory: overall, and after the first 3 % (SIC start-up)."""
import sys, os
d = sys.argv[1]; s = open(os.path.join(d, 'remote_source.h264'), 'rb').read(); r = open(os.path.join(d, 'received.h264'), 'rb').read()
p = q = 0; holes = []; foreign = 0; B = 4096; K = 48
while q < len(r):
    n = min(B, len(r) - q)
    if s[p:p + n] == r[q:q + n]: p += n; q += n; continue
    lo = 0
    while lo < n and p + lo < len(s) and s[p + lo] == r[q + lo]: lo += 1
    p += lo; q += lo
    if q >= len(r): break
    j = s.find(r[q:q + K], p, p + 4000000)
    if j < 0: foreign += 1; q += 1
    else: holes.append((p, j - p)); p = j
tail = len(s) - p; tot = sum(h[1] for h in holes) + tail; cut = 0.03 * len(s)
late = [h for h in holes if h[0] >= cut]
print('source %d B, received %d B, missing %.3f %% (%d holes, tail %d B), foreign %d B' % (len(s), len(r), 100.0 * tot / len(s), len(holes), tail, foreign))
print('after the first 3 %%: %d holes, %.3f %% missing; hole positions (%% of file) %s' % (len(late), 100.0 * (sum(h[1] for h in late) + tail) / (len(s) - cut), [round(100.0 * h[0] / len(s), 1) for h in holes[:30]]))
