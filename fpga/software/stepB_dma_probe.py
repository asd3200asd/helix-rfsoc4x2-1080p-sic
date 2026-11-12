import os, sys, time, struct, argparse
import numpy as np
sys.argv = [sys.argv[0]] + sys.argv[1:]
LOG = open("stepB.log", "a")
def P(*a):
    s = " ".join(str(x) for x in a); print(s, flush=True); LOG.write(s + "\n"); LOG.flush(); os.fsync(LOG.fileno())
import network_layer
from pynq import MMIO, Overlay, allocate
import nl_tap_bridge as B
class A: fpga_ip="192.168.10.2"; host_ip="192.168.10.1"; prefix=24; fpga_mac="02:00:00:00:10:02"; host_mac="02:00:00:00:10:01"; data_port=10001; control_port=10000; metadata_port=10002
P("=== stepB", sys.argv[1:], time.strftime("%H:%M:%S"))
ol = Overlay("helix_nlsg.bit", download=False)
nl = B.configure_network(ol, A); P("network configured", dict(nl.get_network_info()))
d = MMIO(0xB0100000, 0x1000)
B.dma_reset(d); P("dma reset ok SR mm2s/s2mm = 0x%08x 0x%08x" % (d.read(4), d.read(0x34)))
desc = allocate(shape=(4, 16), dtype=np.uint32); buf = allocate(shape=(4, 2048), dtype=np.uint8)
desc[:] = 0; buf[:] = 0
P("desc phys 0x%x buf phys 0x%x" % (desc.device_address, buf.device_address))
mac_h = bytes.fromhex("020000001001"); ip_h = bytes([192,168,10,1]); ip_f = bytes([192,168,10,2])
arp = b"\xff"*6 + mac_h + b"\x08\x06" + struct.pack(">HHBBH", 1, 0x0800, 6, 4, 1) + mac_h + ip_h + b"\x00"*6 + ip_f
n = len(arp); buf[0, :n] = np.frombuffer(arp, dtype=np.uint8); buf.flush()
# descriptor 0: TX (self-linked), descriptor 2: RX (self-linked)
desc[0, 0] = desc.device_address; desc[0, 2] = buf.device_address; desc[0, 6] = n | (1 << 27) | (1 << 26)
desc[2, 0] = desc.device_address + 128; desc[2, 2] = buf.device_address + 2 * 2048; desc[2, 6] = 2048
desc.flush()
if "--rx" in sys.argv:
    P("arming S2MM"); d.write(0x38, desc.device_address + 128); d.write(0x3C, 0); d.write(0x30, 0x00010001)
    time.sleep(0.05); P("S2MM SR 0x%08x" % d.read(0x34)); d.write(0x40, desc.device_address + 128); d.write(0x44, 0)
    time.sleep(0.05); P("S2MM armed SR 0x%08x" % d.read(0x34))
P("starting MM2S"); d.write(0x08, desc.device_address); d.write(0x0C, 0); d.write(0x00, 0x00010001)
time.sleep(0.05); P("MM2S SR 0x%08x" % d.read(4)); d.write(0x10, desc.device_address); d.write(0x14, 0)
time.sleep(0.2); desc.invalidate()
P("MM2S after: SR 0x%08x desc status 0x%08x" % (d.read(4), int(desc[0, 7])))
if "--rx" in sys.argv:
    time.sleep(0.3); desc.invalidate(); buf.invalidate(); st = int(desc[2, 7])
    P("S2MM after: SR 0x%08x desc status 0x%08x" % (d.read(0x34), st))
    if st & 0x80000000: P("RX %d bytes: %s" % (st & 0xFFFF, bytes(buf[2, :min(st & 0xFFFF, 64)]).hex()))
try: P("nl stats", {k: v for k, v in dict(nl.get_debug_stats).items() if "eth" in k.lower() or "arp" in k.lower()})
except Exception as e: P("stats err", e)
d.write(0x00, 0); d.write(0x30, 0); B.dma_reset(d); P("STEP_B_DONE halted SR 0x%08x 0x%08x" % (d.read(4), d.read(0x34)))
