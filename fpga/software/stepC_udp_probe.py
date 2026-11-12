import socket, time, json
import network_layer
from pynq import MMIO, Overlay
ol = Overlay("helix_nlsg.bit", download=False); nl = ol.qsfp_networklayer
gp = MMIO(0xB0120000, 0x1000); sm = MMIO(0xB000A000, 0x1000)
def snap():
    st = nl.get_debug_stats
    st = st() if callable(st) else st
    flat = {p + "." + k: v.get("packets", v) if isinstance(v, dict) else v for p, d in dict(st).items() for k, v in dict(d).items()}
    return flat, [hex(sm.read(4 * i)) for i in range(8)], hex(gp.read(0)), hex(gp.read(8))
a = snap(); print("before", json.dumps(a[0]), a[1:], flush=True)
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("192.168.10.1", 10001)); s.settimeout(0.5)
for i in range(3):
    s.sendto(bytes([(i + j) & 0xFF for j in range(1024)]), ("192.168.10.2", 10001)); time.sleep(0.05)
time.sleep(0.5); b = snap(); print("after ", json.dumps(b[0]), b[1:], flush=True)
print("delta", {k: b[0][k] - a[0][k] for k in b[0] if isinstance(b[0][k], int) and b[0][k] != a[0].get(k)})
print("sockets", dict(nl.read_socket_table()) if hasattr(nl, "read_socket_table") else None)
