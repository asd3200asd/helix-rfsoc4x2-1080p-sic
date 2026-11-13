#!/usr/bin/env python3
"""Board-side access to the SIC block (helix_nlsg_sic1.bit, AXI-Lite 0xB0013000) through /dev/mem.
NOT YET RUN ON THE BOARD.  Needs root.  Does not load a bitstream and does not touch RFDC or clocks.
  sic_ctl.py status
  sic_ctl.py enable | disable
  sic_ctl.py delay <samples>                (reference delay register; total = value + 10)
  sic_ctl.py coef <re0> <im0> [<re1> <im1> ...]   (Q15 integers; unspecified taps = 0; commits)
  sic_ctl.py monitor [n]                    (power before/after SIC, n windows of 2**22 beats)
  sic_ctl.py capture <out.npz> [src] [thr]  (src 0: reference+RX input, 1: RX delayed+SIC output)
"""
import mmap, os, struct, sys, time
BASE, SPAN = 0xB0013000, 0x1000


class Sic:
    def __init__(self):
        fd = os.open('/dev/mem', os.O_RDWR | os.O_SYNC)
        self.m = mmap.mmap(fd, SPAN, offset=BASE)
        self.w = memoryview(self.m).cast('I')      # one 32-bit bus access per read/write
        ident = self.rd(0x00)
        if ident != 0x51C00001:
            raise SystemExit(f'SIC block not found at {BASE:#x} (ID {ident:#010x}); is helix_nlsg_sic1.bit loaded?')
        info = self.rd(0x04)
        self.taps, self.dw, self.cw, self.cf = info & 0xFF, (info >> 8) & 0xFF, (info >> 16) & 0xFF, info >> 24

    def rd(self, off): return self.w[off >> 2]
    def wr(self, off, val): self.w[off >> 2] = val & 0xFFFFFFFF

    def status(self):
        st = self.rd(0x18)
        return dict(taps=self.taps, enable=self.rd(0x08) & 1, delay=self.rd(0x10), epoch=self.rd(0x14),
                    capture_busy=(st >> 1) & 1, capture_done=(st >> 2) & 1,
                    coef=[(self._s(self.rd(0x100 + 8 * k)), self._s(self.rd(0x104 + 8 * k))) for k in range(self.taps)])

    @staticmethod
    def _s(v): return v - (1 << 32) if v & 0x80000000 else v

    def set_coef(self, pairs):
        pairs = list(pairs); pairs += [(0, 0)] * (self.taps - len(pairs))
        for k, (re, im) in enumerate(pairs[:self.taps]):
            self.wr(0x100 + 8 * k, re); self.wr(0x104 + 8 * k, im)
        self.wr(0x0C, 1)

    def monitor(self):
        while True:
            t0 = self.rd(0x18) & 1
            pin = self.rd(0x20) | self.rd(0x24) << 32
            pout = self.rd(0x28) | self.rd(0x2C) << 32
            clips = self.rd(0x30)
            if self.rd(0x18) & 1 == t0:
                return pin, pout, clips

    def capture(self, src=0, thr=0, timeout=10.0):
        import numpy as np
        self.wr(0x44, thr); self.wr(0x40, (src & 1) << 1 | 1)
        t = time.time()
        while not (self.rd(0x18) >> 2) & 1:
            if time.time() - t > timeout: raise SystemExit('capture did not trigger (threshold never exceeded?)')
            time.sleep(0.001)
        n = 4096
        raw = np.zeros((n, 4), dtype=np.uint32)
        for k in range(n):
            self.wr(0x48, k)
            raw[k] = [self.rd(0x50), self.rd(0x54), self.rd(0x58), self.rd(0x5C)]
        w = raw.view(np.int16).reshape(n, 8)        # i0 q0 i1 q1 (A), i0 q0 i1 q1 (B)
        a = (w[:, 0:4].reshape(-1, 2)).astype(np.int64); b = (w[:, 4:8].reshape(-1, 2)).astype(np.int64)
        return a, b                                  # (8192, 2) [re, im] each, time ordered


if __name__ == '__main__':
    a = sys.argv[1:] or ['status']
    s = Sic()
    if a[0] == 'status': print(s.status())
    elif a[0] == 'enable': s.wr(0x08, 1)
    elif a[0] == 'disable': s.wr(0x08, 0)
    elif a[0] == 'delay': s.wr(0x10, int(a[1]))
    elif a[0] == 'coef':
        v = [int(x) for x in a[1:]]; s.set_coef(zip(v[0::2], v[1::2]))
    elif a[0] == 'monitor':
        import math
        for _ in range(int(a[1]) if len(a) > 1 else 1):
            last = s.rd(0x18) & 1
            while s.rd(0x18) & 1 == last: time.sleep(0.002)
            pin, pout, clips = s.monitor()
            print(f'in={pin} out={pout} clips={clips} cancel_dB={10 * math.log10(pin / pout) if pin and pout else float("nan"):.2f}')
    elif a[0] == 'capture':
        import numpy as np
        src = int(a[2]) if len(a) > 2 else 0; thr = int(a[3]) if len(a) > 3 else 0
        A, B = s.capture(src, thr)
        np.savez(a[1], a=A, b=B, src=src, delay=s.rd(0x10), enable=s.rd(0x08) & 1)
        print('saved', a[1], 'rms A', float(np.sqrt((A ** 2).sum(1).mean())), 'rms B', float(np.sqrt((B ** 2).sum(1).mean())))
    else: raise SystemExit(__doc__)
