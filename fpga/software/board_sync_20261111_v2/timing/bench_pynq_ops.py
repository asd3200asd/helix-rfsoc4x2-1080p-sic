# Claude 2026-11-11: cost of the PYNQ primitives the bridge uses per frame (no DMA activity; read-only MMIO on the DMA SR)
import time, numpy as np, os, select
import network_layer
from pynq import MMIO, Overlay, allocate
ol = Overlay("helix_nlsg_s6.bit", download=False)
desc = allocate(shape=(64, 16), dtype=np.uint32); buf = allocate(shape=(10240,), dtype=np.uint8); mm = MMIO(0xB0100000, 0x1000)
def t(name, fn, n=2000):
    t0 = time.perf_counter()
    for _ in range(n): fn()
    print("%-34s %8.1f us" % (name, (time.perf_counter() - t0) / n * 1e6))
frame = bytes(1394)
t("desc.invalidate()", desc.invalidate); t("desc.flush()", desc.flush); t("buf.invalidate() 10 kB", buf.invalidate); t("buf.flush() 10 kB", buf.flush)
t("int(desc[3, 7])", lambda: int(desc[3, 7])); t("desc[3, 7] = 0", lambda: desc.__setitem__((3, 7), 0)); t("mmio.read(SR)", lambda: mm.read(4))
t("buf[:n] = memoryview(frame)", lambda: buf.__setitem__(slice(0, 1394), memoryview(frame))); t("bytes(buf[:n])", lambda: bytes(buf[:1394]))
r, w = os.pipe(); t("select on idle fd, timeout 0", lambda: select.select([r], [], [], 0)); t("time.monotonic()", time.monotonic)
print("buffer type", type(buf).__mro__[0].__name__, "device", type(buf.device).__name__ if hasattr(buf, "device") else "?")
