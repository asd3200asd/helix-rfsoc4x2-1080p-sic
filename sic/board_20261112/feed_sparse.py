"""Claude 2026-11-12: throttled feeder for the local transmit data (sparse local bursts).
usage: feed_sparse.py <file> <delay_s> <tb_per_chunk> <interval_ms>   -> stdout"""
import sys, time
b = open(sys.argv[1], 'rb').read(); d, n, iv = float(sys.argv[2]), int(sys.argv[3]) * 1337, float(sys.argv[4]) / 1e3
time.sleep(d); t = time.monotonic(); o = sys.stdout.buffer
for i in range(0, len(b), n):
    o.write(b[i:i + n]); o.flush(); t += iv
    while time.monotonic() < t: time.sleep(0.0005)
