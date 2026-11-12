"""Compare xsim output (../sim/<case>.out) with the golden model, bit for bit."""
import glob, json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
CASES = os.path.join(HERE, '..', 'cases'); SIM = os.path.join(HERE, '..', 'sim')
WLOG = 8; W = 1 << WLOG
ok_all = True; report = []
for mp in sorted(glob.glob(os.path.join(CASES, '*', 'meta.json'))):
    m = json.load(open(mp)); name = m['name']
    op = os.path.join(SIM, name + '.out')
    if not os.path.exists(op):
        report.append((name, 'NO OUTPUT')); ok_all = False; continue
    exp = np.loadtxt(os.path.join(CASES, name, 'exp.txt'), dtype=np.int64)
    out = np.array([[int(v) for v in ln.split()] for ln in open(op)], dtype=object)
    nb = m['beats']; off = m['latency'] + 1
    if len(out) < nb + off:
        report.append((name, f'SHORT OUTPUT {len(out)} rows')); ok_all = False; continue
    got = out[off:off + nb, 0:9].astype(np.int64)
    bad = np.argwhere(got != exp)
    # monitor: every latch must equal the sum over the previous W dumped beats
    o = out.astype(object)
    d2 = np.array([sum(int(v) ** 2 for v in r[4:8]) for r in o], dtype=object)
    e2 = np.array([sum(int(v) ** 2 for v in r[0:4]) for r in o], dtype=object)
    cl = np.array([1 if int(r[8]) else 0 for r in o])
    tog = np.array([int(r[10]) for r in o])
    latches = [r for r in range(1, len(o)) if tog[r] != tog[r - 1]]
    mon_bad = 0; mon_n = 0
    for r in latches:
        if r - W - 1 < 0: continue
        mon_n += 1
        s = slice(r - W - 1, r - 1)
        if int(o[r][11]) != sum(d2[s]) or int(o[r][12]) != sum(e2[s]) or int(o[r][13]) != int(cl[s].sum()):
            mon_bad += 1
    epoch_ok = int(o[-1][9]) == m['commits']
    good = len(bad) == 0 and mon_bad == 0 and mon_n > 0 and epoch_ok
    ok_all &= good
    report.append((name, f"{'PASS' if good else 'FAIL'}  beats={nb} taps={m['taps']} delay={m['delay']} "
                         f"mismatching_values={len(bad)} first={bad[0].tolist() if len(bad) else None} "
                         f"monitor_windows={mon_n} monitor_bad={mon_bad} epoch_ok={epoch_ok}"))
for n, r in report: print(f'{n:22s} {r}')
print('ALL PASS' if ok_all else 'SOME FAILED')
open(os.path.join(SIM, 'check_result.txt'), 'w').write('\n'.join(f'{n} {r}' for n, r in report) + ('\nALL PASS\n' if ok_all else '\nSOME FAILED\n'))
sys.exit(0 if ok_all else 1)
