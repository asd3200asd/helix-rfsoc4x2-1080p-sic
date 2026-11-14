"""Claude 2026-11-13: feed <run_dir>/received.h264 to the DisplayPort player and keep it live.
usage: python3 dp_feed.py <file> [max_lag_bytes=1000000] | ffplay ... -i pipe:0
Starts at the last SPS (so decoding can start at once), then streams new bytes as they are written.  When the player falls more
than max_lag_bytes behind (1 MB = about 3 s of the camera stream), it jumps to the newest SPS/IDR, so the screen never lags
far behind even when the board cannot decode every frame."""
import os, sys, time
f = sys.argv[1]; maxlag = int(sys.argv[2]) if len(sys.argv) > 2 else 1_000_000
out = sys.stdout.buffer
while not os.path.exists(f): time.sleep(0.1)
fd = open(f, 'rb')
def last_sps(end):
    start = max(0, end - 16_000_000); fd.seek(start); b = fd.read(end - start); i = b.rfind(b'\x00\x00\x00\x01\x67')
    return start + i if i >= 0 else None
p = last_sps(os.path.getsize(f)); pos = p if p is not None else 0; skips = 0
while True:
    sz = os.path.getsize(f)
    if sz - pos > maxlag:
        p = last_sps(sz)
        if p is not None and p > pos:
            skips += 1; print('DP_SKIP %d: %d bytes' % (skips, p - pos), file=sys.stderr, flush=True); pos = p
    if sz > pos:
        fd.seek(pos); b = fd.read(min(sz - pos, 131072)); pos += len(b)
        try: out.write(b); out.flush()
        except (BrokenPipeError, OSError): break
    else: time.sleep(0.02)
