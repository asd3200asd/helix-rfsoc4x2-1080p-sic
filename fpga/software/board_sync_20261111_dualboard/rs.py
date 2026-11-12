import sys, collections
P=collections.OrderedDict(); B={}
for l in open(sys.argv[1], errors='replace'):
    if 'RSWEEP' in l:
        d=dict(x.split('=') for x in l.split()[1:]); v=int(d['val']); P[v]=P.get(v,0)+int(d['packets']); B[v]=B.get(v,0)+int(d['bad'])
        P.setdefault(('n',v),0); P[('n',v)]+=1
for v in [k for k in P if not isinstance(k,tuple)]:
    print("val=0x%08X (%d)  steps=%d  packets=%d  bad=%d  bad%%=%.2f"%(v, v&0x7fffffff, P[('n',v)], P[v], B[v], 100*B[v]/max(1,P[v])))
