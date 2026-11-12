import sys, time
from pynq import MMIO, Overlay
def afi():
    out = {}
    for name, base in (("HP0", 0xFD380000), ("HP1", 0xFD390000)):
        m = MMIO(base, 0x1000); out[name] = (m.read(0x00) & 3, m.read(0x14) & 3)
    return out
print("AFI before (rd,wr; 0=128b 1=64b 2=32b):", afi(), flush=True)
if "--load" in sys.argv:
    ol = Overlay("helix_nlsg.bit"); print("LOADED", flush=True); time.sleep(1)
    print("AFI after:", afi(), flush=True)
    d = MMIO(0xB0100000, 0x1000)
    print("DMA MM2S CR/SR = 0x%08x 0x%08x  S2MM CR/SR = 0x%08x 0x%08x" % (d.read(0), d.read(4), d.read(0x30), d.read(0x34)), flush=True)
    print("ip_dict has:", [k for k in ol.ip_dict if "nl_eth" in k or "networklayer" in k], flush=True)
print("STEP_A_DONE", flush=True)
