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
NDESC, BUFSZ = 64, 2048        # descriptors per ring, bytes per buffer (>= MTU 1500 + 14)
TUNSETIFF, IFF_TAP, IFF_NO_PI = 0x400454CA, 0x0002, 0x1000


class Ring:
    def __init__(self, allocate, mmio, base):
        import numpy as np
        self.mmio, self.base = mmio, base
        self.desc = allocate(shape=(NDESC, 16), dtype=np.uint32)   # 64-byte aligned descriptors
        self.buf = allocate(shape=(NDESC, BUFSZ), dtype=np.uint8)
        if (self.desc.device_address | self.buf.device_address) >> 32:
            raise RuntimeError("CMA buffer above 4 GiB; DMA is 32-bit")
        self.desc[:] = 0
        for i in range(NDESC):
            self.desc[i, D_NXT] = self.daddr((i + 1) % NDESC)
            self.desc[i, D_BUF] = self.buf.device_address + i * BUFSZ
        self.desc.flush()
        self.idx = 0

    def daddr(self, i):
        return self.desc.device_address + i * 64

    def rd(self, off):
        return self.mmio.read(self.base + off)

    def wr(self, off, v):
        self.mmio.write(self.base + off, v)

    def start(self):
        self.wr(CURDESC, self.daddr(0)); self.wr(CURDESC_MSB, 0)
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
    t_end = time.monotonic() + a.seconds if a.seconds else None
    print(f"BRIDGE_READY tap={a.tap} host={a.host_ip} fpga={a.fpga_ip}", flush=True)
    try:
        while not stop and (t_end is None or time.monotonic() < t_end):
            # FPGA -> Linux
            got = False
            while True:
                rx.desc.invalidate()
                sts = int(rx.desc[rx.idx, D_STS])
                if not sts & STS_CMPLT:
                    break
                if sts & STS_ERR:
                    raise RuntimeError(f"S2MM descriptor error status=0x{sts:08x}")
                n = sts & LEN_MASK
                rx.buf.invalidate()
                try:
                    os.write(tap, bytes(rx.buf[rx.idx, :n]))
                except OSError:
                    drop += 1
                rx.desc[rx.idx, D_STS] = 0
                rx.desc.flush()
                rx.kick(rx.idx)
                rx.idx = (rx.idx + 1) % NDESC
                n_rx += 1; got = True
            # Linux -> FPGA
            r, _, _ = select.select([tap], [], [], 0 if got else 0.001)
            if r:
                i = tx.idx
                if tx_busy[i]:
                    tx.desc.invalidate()
                    if not int(tx.desc[i, D_STS]) & STS_CMPLT:
                        continue                          # ring full: let the kernel queue hold it
                try:
                    frame = os.read(tap, BUFSZ)
                except BlockingIOError:
                    continue
                n = len(frame)
                tx.buf[i, :n] = memoryview(frame)
                tx.buf.flush()
                tx.desc[i, D_STS] = 0
                tx.desc[i, D_CTRL] = n | CTRL_SOF | CTRL_EOF
                tx.desc.flush()
                tx.kick(i)
                tx_busy[i] = True
                tx.idx = (i + 1) % NDESC
                n_tx += 1
            tx.check("MM2S"); rx.check("S2MM")
    finally:
        mmio.write(MM2S + CR, 0); mmio.write(S2MM + CR, 0)
        dma_reset(mmio)                                    # leave both channels halted
        os.close(tap)
        print(f"BRIDGE_STOPPED tx_frames={n_tx} rx_frames={n_rx} tap_drops={drop}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
