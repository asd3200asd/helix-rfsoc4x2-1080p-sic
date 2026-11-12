import sys, zlib, numpy as np
PAY=1337; run, rf = sys.argv[1], sys.argv[2]
b=open(rf,'rb').read(); cr=np.array([zlib.crc32(b[i*PAY:(i+1)*PAY])&0xFFFFFFFF for i in range(len(b)//PAY)],dtype=np.uint64)
rec=np.fromfile(run+'/pkt.bin',dtype=np.dtype([('t','<u8'),('ok','u1'),('z','u1'),('sq','<u2'),('c','<u4')]))
if not len(rec): print('no packets'); sys.exit()
sq=rec['sq'].astype(np.int64); ok=rec['ok']==1; isr=ok&(sq<len(cr))&(cr[np.minimum(sq,len(cr)-1)]==rec['c'])
t=(rec['t']-rec['t'][0])/1e9; dur=t[-1]
print('packets',len(rec),'duration %.1fs'%dur,'pkts/s %.0f'%(len(rec)/dur),'remote_good',int(isr.sum()),'good_other',int((ok&~isr).sum()),'bad',int((~ok).sum()),'bad%% %.2f'%(100*(~ok).mean()),'remote_unique',len(np.unique(sq[isr])))
