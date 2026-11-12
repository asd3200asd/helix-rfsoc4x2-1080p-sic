"""Claude 2026-11-12: summarise tb_psslock runs.  usage: summarize.py <build_dir>
A slot is healthy when the OFDM output record (count, hash, phase of lastOut) equals that of an undisturbed slot."""
import sys, re, glob, os
P = 45000; PERT = 3; NS = 8
for f in sorted(glob.glob(os.path.join(sys.argv[1], 'xsim_*.log')), key=lambda s: int(re.findall(r'xsim_(\d+)\.log', s)[0])):
    e = int(re.findall(r'xsim_(\d+)\.log', f)[0]); ev = [l for l in open(f, errors='replace') if 'EV ' in l]
    ol = [dict((k, int(v)) for k, v in re.findall(r'(\w+)=(-?\d+)', l)) for l in ev if 'EV OLAST' in l]
    done = any('EV DONE' in l for l in ev); fe = sum('FIFOERR' in l for l in ev); ps = sum('PSSTART' in l for l in ev); pl = sum('PSLAST' in l for l in ev)
    ref = [(o['ph'], o['cnt'], o['hash']) for o in ol if o['n'] // P == 2]
    if not ref: print(e, 'no reference slot', len(ol)); continue
    ref = ref[0]; after = [o for o in ol if o['n'] >= (PERT + 1) * P]
    good = sum((o['ph'], o['cnt'], o['hash']) == ref for o in after)
    exp = NS - (PERT + 1)
    print('EXTRA=%-10s done=%s  slot starts=%d ends=%d fifo_err=%d | slots after the disturbed one: output records=%d (expected %d), identical to reference=%d | %s' % (
        'none' if e > 1e8 else e, done, ps, pl, fe, len(after), exp, good, 'HEALTHY' if good == exp and len(after) == exp else 'BROKEN'))
    if good != exp: print('      first records after:', [(o['ph'], o['cnt']) for o in after[:4]], ' reference (ph,cnt)=', ref[:2])
