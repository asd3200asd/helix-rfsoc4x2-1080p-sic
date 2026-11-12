"""Claude 2026-11-12: compare every scenario's event log with the undisturbed run of the same build.
usage: compare.py <build_dir> [<other_build_dir_for_cross_check>]"""
import sys, glob, os, re
def ev(f): return [l.split('EV ', 1)[1].strip() for l in open(f, errors='replace') if 'EV ' in l and 'EV EXTRA' not in l]
d = sys.argv[1]; ref = ev(os.path.join(d, 'xsim_900000000.log'))
print(d, 'undisturbed run: %d events, %d slot starts, %d output records' % (len(ref), sum(e.startswith('PSSTART') for e in ref), sum(e.startswith('OLAST') for e in ref)))
if len(sys.argv) > 2:
    o = ev(os.path.join(sys.argv[2], 'xsim_900000000.log')); print('  undisturbed run identical to', sys.argv[2], ':', o == ref)
for f in sorted(glob.glob(os.path.join(d, 'xsim_*.log')), key=lambda s: int(re.findall(r'xsim_(\d+)\.log', s)[0])):
    e = int(re.findall(r'xsim_(\d+)\.log', f)[0])
    if e > 1e8: continue
    x = ev(f); done = 'DONE' in x
    if x == ref: print('  EXTRA=%6d  identical to the undisturbed run (extra detection dropped)' % e); continue
    a = [k for k in x if k not in ref]; b = [k for k in ref if k not in x]
    print('  EXTRA=%6d  done=%s  differs: %d events only here, %d missing' % (e, done, len(a), len(b)))
    for k in a[:6]: print('       + ' + k)
    for k in b[:6]: print('       - ' + k)
