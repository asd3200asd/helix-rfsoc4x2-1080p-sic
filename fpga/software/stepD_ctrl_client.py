import socket, struct, sys, time
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("192.168.10.1", 10000)); s.settimeout(2.0)
def cmd(line, reply=False):
    s.sendto((line + " \n").encode(), ("192.168.10.2", 10000))
    if reply:
        try: d, _ = s.recvfrom(64); return d
        except socket.timeout: return None
    time.sleep(0.02)
r = cmd("initPlatform", True); print("initPlatform ->", r)
r = cmd("readReg 0xB000A018", True); print("readReg SM+0x18 ->", r and struct.unpack("<I", r[:4])[0])
cmd("writeReg 0xB000A018 1234")
r = cmd("readReg 0xB000A018", True); print("after writeReg 1234 ->", r and struct.unpack("<I", r[:4])[0])
cmd("writeReg 0xB000A018 0")
print("STEP_D_DONE")
