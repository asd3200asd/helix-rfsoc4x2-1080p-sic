"""Claude 2026-11-12: align a received raw stream against the source file and list holes / foreign data.
usage: filecmp.py <source> <received>"""
import sys
s = open(sys.argv[1], 'rb').read(); r = open(sys.argv[2], 'rb').read()
p = q = 0; holes = []; foreign = 0; B = 4096; K = 48
while q < len(r):
    n = min(B, len(r) - q)
    if s[p:p + n] == r[q:q + n]: p += n; q += n; continue
    # advance to the first differing byte
    lo = 0
    while lo < n and p + lo < len(s) and s[p + lo] == r[q + lo]: lo += 1
    p += lo; q += lo
    if q >= len(r): break
    j = s.find(r[q:q + K], p, p + 4000000)
    if j < 0: foreign += 1; q += 1; 
    else: holes.append((p, j - p)); p = j
miss = sum(h[1] for h in holes) + (len(s) - p)
print('source %d bytes, received %d bytes; holes %d (%d bytes = %.3f %%), tail not received %d bytes, foreign bytes %d' % (len(s), len(r), len(holes), sum(h[1] for h in holes), 100.0 * sum(h[1] for h in holes) / len(s), len(s) - p, foreign))
print('first hole at source offset', holes[0] if holes else None, '; hole sizes:', sorted(set(h[1] for h in holes))[:8], '; hole positions (% of file):', [round(100.0 * h[0] / len(s), 1) for h in holes[:40]])
