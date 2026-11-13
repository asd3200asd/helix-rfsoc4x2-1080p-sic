#!/usr/bin/env python3
"""TAP <-> SG-DMA bridge for the helix_nlsg overlay (Claude, 2026-11-10).

The PL NetworkLayer (ARP/ICMP/UDP) now has one pair of AXI DMA scatter-gather
channels where the 100G MAC used to be:

  Linux TAP "helix0"  --MM2S-->  NetworkLayer S_AXIS_eth2nl   (frames "received" by the FPGA)
  Linux TAP "helix0"  <--S2MM--  NetworkLayer M_AXIS_nl2eth   (frames "sent" by the FPGA)

So anything on the ARM (the original HELIX C++ host program, ping, a UDP sender)
talks to the FPGA UDP stack exactly as the PC did over 10G/100G Ethernet.

Run on the board:
  sudo env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B \
      nl_tap_bridge.py helix_nlsg.bit            # loads the bit, configures, bridges
  ... --no-download                              # bit already loaded
Then in another shell:  ping 192.168.10.2
"""
import argparse, fcntl, ipaddress, os, select, signal, struct, subprocess, sys, time

DMA_BASE = 0xB0100000          # nl_eth_dma S_AXI_LITE (old CMAC slot)
MM2S, S2MM = 0x00, 0x30        # channel register bases
CR, SR, CURDESC, CURDESC_MSB, TAILDESC, TAILDESC_MSB = 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14
CR_RS, CR_RESET = 0x1, 0x4
SR_HALTED, SR_IDLE, SR_SGINCLD, SR_ERRMASK = 0x1, 0x2, 0x8, 0x770
D_NXT, D_NXT_MSB, D_BUF, D_BUF_MSB, D_CTRL, D_STS = 0, 1, 2, 3, 6, 7   # 32-bit word index
CTRL_SOF, CTRL_EOF, STS_CMPLT, STS_ERR = 1 << 27, 1 << 26, 1 << 31, 0x7 << 28
LEN_MASK = 0xFFFF              # c_sg_length_width = 16
NDESC, BUFSZ = 64, 10240       # descriptors per ring, bytes per buffer (jumbo: HELIX uses 8192-byte UDP payloads)
TUNSETIFF, IFF_TAP, IFF_NO_PI = 0x400454CA, 0x0002, 0x1000


class Ring:
    def __init__(self, allocate, mmio, base):
        import numpy as np
        self.mmio, self.base = mmio, base
        self.desc = allocate(shape=(NDESC, 16), dtype=np.uint32)   # 64-byte aligned descriptors
        # Claude 2026-11-11: one buffer object per descriptor.  flush()/invalidate() sync a whole buffer object, and with a
        # single 64 x 10240-byte object every frame paid for 655 kB of cache maintenance (~0.3 ms per frame).
        self.bufs = [allocate(shape=(BUFSZ,), dtype=np.uint8) for _ in range(NDESC)]
        if self.desc.device_address >> 32 or any(b.device_address >> 32 for b in self.bufs):
            raise RuntimeError("CMA buffer above 4 GiB; DMA is 32-bit")
        self.desc[:] = 0
        for i in range(NDESC):
            self.desc[i, D_NXT] = self.daddr((i + 1) % NDESC)
            self.desc[i, D_BUF] = self.bufs[i].device_address
        self.desc.flush()
        self.idx = 0
        # Claude 2026-11-11 (speed): plain ndarray views avoid PynqBuffer indexing overhead (~50 us per access), and
        # HELIX_NOSYNC=1 skips flush()/invalidate() (each is a ~54 us XRT sync ioctl).  NOSYNC is only valid while the
        # buffer objects are mapped uncached/write-combined; it is verified by byte-exact tests, not assumed.
        self.d = self.desc.view(np.ndarray); self.b = [x.view(np.ndarray) for x in self.bufs]
        self.sync = not os.environ.get("HELIX_NOSYNC"); self.reg = mmio.array

    def daddr(self, i):
        return self.desc.device_address + i * 64

    def rd(self, off):
        return int(self.reg[(self.base + off) >> 2])

    def wr(self, off, v):
        self.reg[(self.base + off) >> 2] = v

    def start(self):
        # Claude 2026-11-11: never set RS unless the channel is halted and CURDESC really holds our ring address;
        # a DMA that starts from a stale/zero CURDESC would fetch descriptors from (and write frames to) arbitrary memory.
        if not self.rd(SR) & SR_HALTED:
            raise RuntimeError(f"DMA channel 0x{self.base:02x} is not halted before start, SR=0x{self.rd(SR):08x}")
        self.wr(CURDESC, self.daddr(0)); self.wr(CURDESC_MSB, 0)
        if self.rd(CURDESC) != self.daddr(0) or self.rd(CURDESC_MSB) != 0:
            raise RuntimeError(f"DMA channel 0x{self.base:02x} CURDESC readback 0x{self.rd(CURDESC):08x} != 0x{self.daddr(0):08x}")
        self.wr(CR, (self.rd(CR) & ~0x7000) | CR_RS)          # no interrupts, polling
        t0 = time.monotonic()
        while self.rd(SR) & SR_HALTED:
            if time.monotonic() - t0 > 1.0:
                raise RuntimeError(f"DMA channel 0x{self.base:02x} did not start, SR=0x{self.rd(SR):08x}")

    def kick(self, i):
        self.wr(TAILDESC, self.daddr(i)); self.wr(TAILDESC_MSB, 0)

    def check(self, name):
        sr = self.rd(SR)
        if sr & SR_ERRMASK:
            raise RuntimeError(f"{name} DMA error, SR=0x{sr:08x}")


def dma_reset(mmio):
    mmio.write(MM2S + CR, CR_RESET)
    t0 = time.monotonic()
    while (mmio.read(MM2S + CR) | mmio.read(S2MM + CR)) & CR_RESET:
        if time.monotonic() - t0 > 1.0:
            raise RuntimeError("DMA reset did not clear")
    if not mmio.read(MM2S + SR) & SR_SGINCLD:
        raise RuntimeError("nl_eth_dma is not a scatter-gather DMA (wrong bitstream?)")


def open_tap(name, host_ip, prefix, host_mac):
    fd = os.open("/dev/net/tun", os.O_RDWR | os.O_NONBLOCK)
    fcntl.ioctl(fd, TUNSETIFF, struct.pack("16sH", name.encode(), IFF_TAP | IFF_NO_PI))
    for cmd in (["ip", "link", "set", name, "address", host_mac],
                ["ip", "addr", "replace", f"{host_ip}/{prefix}", "dev", name],
                ["ip", "link", "set", name, "mtu", "9000"],
                ["ip", "link", "set", name, "txqueuelen", "5000"],
                ["ip", "link", "set", name, "up"]):
        subprocess.run(cmd, check=True)
    return fd


def configure_network(overlay, a):
    from network_layer import NetworkLayer
    nl = overlay.qsfp_networklayer
    if not isinstance(nl, NetworkLayer):
        raise RuntimeError(f"NetworkLayer driver did not bind: {type(nl)}")
    net = ipaddress.IPv4Network(f"{a.fpga_ip}/{a.prefix}", strict=False)
    nl.set_mac_address(a.fpga_mac)
    nl.set_ip_address(a.fpga_ip, str(net.network_address + 1))
    nl.register_map.ip_mask = int(net.netmask)
    nl.sockets[:] = ("", 0, 0, False)
    for sid, port in enumerate((a.data_port, a.control_port, a.metadata_port)):
        nl.sockets[sid] = (a.host_ip, port, port, True)
    nl.populate_socket_table()
    nl.write_arp_entry(a.host_mac, a.host_ip)      # FPGA may transmit before the host ever ARPs
    return nl


import numpy as np


def core_loop(mmio, tx, rx, tap, stop, t_end, busy):
    """HELIX_CORE=1: frames are moved by nl_bridge_core.c (libnlbridge.so); Python only watches the clock and signals."""
    import ctypes
    lib = ctypes.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)), "libnlbridge.so"))
    class State(ctypes.Structure):
        _fields_ = [("regs", ctypes.c_void_p), ("txd", ctypes.c_void_p), ("rxd", ctypes.c_void_p),
                    ("txb", ctypes.POINTER(ctypes.c_void_p)), ("rxb", ctypes.POINTER(ctypes.c_void_p)),
                    ("txd_phys", ctypes.c_uint32), ("rxd_phys", ctypes.c_uint32), ("ndesc", ctypes.c_uint32), ("bufsz", ctypes.c_uint32),
                    ("tx_idx", ctypes.c_uint32), ("rx_idx", ctypes.c_uint32), ("tx_busy", ctypes.c_uint8 * 256),
                    ("tap_fd", ctypes.c_int), ("busy_poll", ctypes.c_int),
                    ("n_tx", ctypes.c_uint64), ("n_rx", ctypes.c_uint64), ("drops", ctypes.c_uint64),
                    ("tx_bytes", ctypes.c_uint64), ("rx_bytes", ctypes.c_uint64), ("err_sr", ctypes.c_uint32)]
    lib.nlb_run.argtypes = [ctypes.POINTER(State), ctypes.c_uint64]; lib.nlb_run.restype = ctypes.c_int
    st = State()
    txb = (ctypes.c_void_p * NDESC)(*[b.ctypes.data for b in tx.b]); rxb = (ctypes.c_void_p * NDESC)(*[b.ctypes.data for b in rx.b])
    st.regs = mmio.array.ctypes.data; st.txd = tx.d.ctypes.data; st.rxd = rx.d.ctypes.data; st.txb = txb; st.rxb = rxb
    st.txd_phys = tx.daddr(0); st.rxd_phys = rx.daddr(0); st.ndesc = NDESC; st.bufsz = BUFSZ
    st.tx_idx = tx.idx; st.rx_idx = rx.idx; st.tap_fd = tap; st.busy_poll = 1 if busy else 0
    while not stop and (t_end is None or time.monotonic() < t_end):
        if lib.nlb_run(ctypes.byref(st), 50_000_000) != 0:
            raise RuntimeError(f"DMA error reported by nl_bridge_core, status=0x{st.err_sr:08x}")
    return int(st.n_tx), int(st.n_rx), int(st.drops)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("bitfile")
    p.add_argument("--no-download", action="store_true")
    p.add_argument("--tap", default="helix0")
    p.add_argument("--host-ip", default="192.168.10.1")
    p.add_argument("--fpga-ip", default="192.168.10.2")
    p.add_argument("--prefix", type=int, default=24)
    p.add_argument("--host-mac", default="02:00:00:00:10:01")
    p.add_argument("--fpga-mac", default="02:00:00:00:10:02")
    p.add_argument("--data-port", type=int, default=10001)
    p.add_argument("--control-port", type=int, default=10000)
    p.add_argument("--metadata-port", type=int, default=10002)
    p.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = until SIGINT/SIGTERM)")
    a = p.parse_args()

    import network_layer  # must register the NetworkLayer driver before Overlay()
    from pynq import MMIO, Overlay, allocate
    overlay = Overlay(a.bitfile, download=not a.no_download)
    configure_network(overlay, a)
    mmio = MMIO(DMA_BASE, 0x1000)
    dma_reset(mmio)
    tx, rx = Ring(allocate, mmio, MM2S), Ring(allocate, mmio, S2MM)
    for i in range(NDESC):                               # arm every RX descriptor
        rx.desc[i, D_CTRL] = BUFSZ
    rx.desc.flush()
    rx.start(); rx.kick(NDESC - 1)
    tx.start()
    tx_busy = [False] * NDESC
    tap = open_tap(a.tap, a.host_ip, a.prefix, a.host_mac)
    stop = []
    for s in (signal.SIGINT, signal.SIGTERM):
        signal.signal(s, lambda *_: stop.append(1))
    n_tx = n_rx = drop = 0
    BUSY = bool(os.environ.get("HELIX_BUSY"))
    tlog = [] if os.environ.get("HELIX_BRIDGE_TIMING") else None   # Claude: (dir, bytes, monotonic_ns) per frame, written at exit
    t_end = time.monotonic() + a.seconds if a.seconds else None
    print(f"BRIDGE_READY tap={a.tap} host={a.host_ip} fpga={a.fpga_ip}", flush=True)
    CORE = bool(os.environ.get("HELIX_CORE"))
    try:
        if CORE:
            n_tx, n_rx, drop = core_loop(mmio, tx, rx, tap, stop, t_end, BUSY)
        while not CORE and not stop and (t_end is None or time.monotonic() < t_end):
            # FPGA -> Linux
            got = False
            while True:
                if rx.sync: rx.desc.invalidate()
                sts = int(rx.d[rx.idx, D_STS])
                if not sts & STS_CMPLT:
                    break
                if sts & STS_ERR:
                    raise RuntimeError(f"S2MM descriptor error status=0x{sts:08x}")
                n = sts & LEN_MASK
                if rx.sync: rx.bufs[rx.idx].invalidate()
                try:
                    os.write(tap, rx.b[rx.idx][:n].tobytes())
                except OSError:
                    drop += 1
                rx.d[rx.idx, D_STS] = 0
                if rx.sync: rx.desc.flush()
                rx.kick(rx.idx)
                rx.idx = (rx.idx + 1) % NDESC
                n_rx += 1; got = True
                if tlog is not None: tlog.append(("R", n, time.monotonic_ns()))
            # Linux -> FPGA
            r, _, _ = select.select([tap], [], [], 0 if (got or BUSY) else 0.001)   # HELIX_BUSY=1: never sleep (one CPU core spins)
            if r:
                i = tx.idx
                if tx_busy[i]:
                    if tx.sync: tx.desc.invalidate()
                    if not int(tx.d[i, D_STS]) & STS_CMPLT:
                        continue                          # ring full: let the kernel queue hold it
                try:
                    frame = os.read(tap, BUFSZ)
                except BlockingIOError:
                    continue
                n = len(frame)
                tx.b[i][:n] = np.frombuffer(frame, dtype=np.uint8)
                if tx.sync: tx.bufs[i].flush()
                tx.d[i, D_STS] = 0
                tx.d[i, D_CTRL] = n | CTRL_SOF | CTRL_EOF
                if tx.sync: tx.desc.flush()
                tx.kick(i)
                tx_busy[i] = True
                tx.idx = (i + 1) % NDESC
                n_tx += 1
                if tlog is not None: tlog.append(("T", n, time.monotonic_ns()))
            tx.check("MM2S"); rx.check("S2MM")
    finally:
        mmio.write(MM2S + CR, 0); mmio.write(S2MM + CR, 0)
        dma_reset(mmio)                                    # leave both channels halted
        os.close(tap)
        if tlog is not None:
            with open(os.environ["HELIX_BRIDGE_TIMING"], "w") as f:
                f.writelines("%s %d %d\n" % e for e in tlog)
        print(f"BRIDGE_STOPPED tx_frames={n_tx} rx_frames={n_rx} tap_drops={drop}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
