#!/usr/bin/env python3
"""Linux port of the HELIX PS control application (FreeRTOS v46_helix_qsfp_app) for helix_nlsg.bit.

Receives the original text commands that the HELIX host library sends to UDP port 10000
(PL NetworkLayer socket 1 -> eth_dma_internal S2MM), executes them, and replies through the
same DMA (MM2S -> router -> socket 1).  Run as root AFTER nl_tap_bridge.py has loaded the
bitstream and configured the NetworkLayer:
  sudo env BOARD=RFSoC4x2 XILINX_XRT=/usr /usr/local/share/pynq-venv/bin/python3 -B -u helix_ctrl_service.py helix_nlsg.bit
Claude, 2026-11-10.  Command set and semantics follow cmd_parser.c / tx_driver.c / rx_driver.c / rfdc_driver.c.
"""
import argparse, signal, struct, sys, time
import numpy as np

REG_BASE, REG_SPAN = 0xB0000000, 0x00130000
SM, PKTZ, TX_XBAR, RX_XBAR, MODE_GPIO = 0xB000A000, 0xB0015000, 0xB0030000, 0xB0020000, 0xB0120000
UDP_CMD_ACK, UDP_CMD_FAILED = 16, 3
FIFO_SIZE = 64 * 1024 * 4
GPIO_BANK_OFFSET = 78
HARD_RESET_TX, HARD_RESET_RX, LBM_START, LBM_LOAD_DATA, LBM_LOOPBACK, LBM_RST, ENABLE_RECV = range(7)
SPLIT_6, SPLIT_7_3, SPLIT_7_2, SPLIT_7_2x, SPLIT_8, SPLIT_HW = 1, 2, 3, 4, 5, 6
D = None  # "disabled"
TX_SPLITS = {SPLIT_7_3: [0, 1, 2, 3, D, D], SPLIT_7_2: [D, 0, 2, 3, D, D], SPLIT_7_2x: [D, D, 0, 3, D, D],
             SPLIT_8: [D, D, D, 4, 0, D], SPLIT_HW: [D, D, D, D, D, 0]}
RX_SPLITS = {SPLIT_6: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9], SPLIT_7_3: [0, 1, 2, 3, 4, 5, D, D, 6, 7],
             SPLIT_7_2: [0, 1, 2, 3, D, D, D, D, 4, 5], SPLIT_7_2x: [0, 1, D, D, D, D, D, D, 2, 3],
             SPLIT_8: [D, D, D, D, D, D, D, D, 0, 1], 6: [10, 11, D, D, D, D, D, D, 2, 3]}
ARGT = {"initplatform": "", "setncofreq": "uuudi", "setnyquistzone": "uuuu", "setqmcsettings": "uuuud",
        "writereg": "li", "readreg": "l", "setfreezecal": "i", "triggertx": "u", "triggerrx": "uuuuu",
        "loadssb": "u", "changetxsplit": "u", "changerxsplit": "u", "enablerx": "u", "configudpserver": "uu"}


def log(*a):
    print(time.strftime("%H:%M:%S"), *a, flush=True)


class Service:
    def __init__(self, bitfile, verbose):
        import xrfdc  # noqa: F401  (must be imported BEFORE Overlay() so the RFDC driver binds)
        import network_layer  # noqa: F401  (registers the NetworkLayer driver)
        from pynq import GPIO, MMIO, Overlay, allocate
        self.verbose = verbose
        self.ol = Overlay(bitfile, download=False)
        self.reg = MMIO(REG_BASE, REG_SPAN)
        self.allocate = allocate
        base = GPIO.get_gpio_base() + GPIO_BANK_OFFSET
        self.gpio = [GPIO(base + i, "out") for i in range(7)]
        self.cdma = self.ol.eth_dma_internal.axi_eth_dma_internal     # control DMA (simple mode)
        self.tdma = self.ol.tx_blocks.axi_dma_0                       # SSB/LBM loader DMA (simple mode)
        self.rxbuf = allocate(shape=(16384,), dtype=np.uint8)
        self.txbuf = allocate(shape=(8,), dtype=np.uint8)
        self.rf = self.ol.usp_rf_data_converter_0

    # ---- low level -------------------------------------------------------
    def w(self, addr, val):
        if not REG_BASE <= addr < REG_BASE + REG_SPAN:
            raise ValueError(f"address 0x{addr:x} outside PL register window")
        self.reg.write(addr - REG_BASE, val & 0xFFFFFFFF)

    def r(self, addr):
        if not REG_BASE <= addr < REG_BASE + REG_SPAN:
            raise ValueError(f"address 0x{addr:x} outside PL register window")
        return self.reg.read(addr - REG_BASE)

    def pulse(self, pin, us=10):
        self.gpio[pin].write(1); time.sleep(us * 1e-6); self.gpio[pin].write(0)

    def reply(self, value):
        self.txbuf[:4] = np.frombuffer(struct.pack("<I", value & 0xFFFFFFFF), dtype=np.uint8)
        self.cdma.sendchannel.transfer(self.txbuf, nbytes=4)
        t0 = time.monotonic()
        while not self.cdma.sendchannel.idle:
            if time.monotonic() - t0 > 1.0:
                raise RuntimeError("control reply DMA timeout")

    def recv_packet(self, timeout=None, stop=None):
        """One UDP control payload (bytes) or None on timeout/stop."""
        ch = self.cdma.recvchannel
        if not getattr(self, "_rx_armed", False):
            ch.transfer(self.rxbuf); self._rx_armed = True
        t0 = time.monotonic()
        while not ch.idle:
            if stop and stop():
                return None
            if timeout is not None and time.monotonic() - t0 > timeout:
                return None
            time.sleep(0.0005)
        self._rx_armed = False
        n = self.cdma.mmio.read(0x58)                    # S2MM_LENGTH: bytes actually received
        self.rxbuf.invalidate()
        return bytes(self.rxbuf[:n])

    def set_switch(self, base, table):
        self.w(base, 0)                                  # RegUpdateDisable
        for mi, si in enumerate(table):
            self.w(base + 0x40 + 4 * mi, 0x80000000 if si is None else si)
        self.w(base, 0x2)                                # RegUpdateEnable (commit)

    def block(self, typ, tile, blk):
        tiles = self.rf.adc_tiles if typ == 0 else self.rf.dac_tiles
        return tiles[tile], tiles[tile].blocks[blk]

    def set_mixer(self, typ, tile, blk, freq):
        x = self.xrfdc; t, b = self.block(typ, tile, blk)
        b.MixerSettings = {"CoarseMixFreq": x.COARSE_MIX_BYPASS, "EventSource": x.EVNT_SRC_TILE if typ == 0 else x.EVNT_SRC_IMMEDIATE,
                           "FineMixerScale": x.MIXER_SCALE_1P0, "Freq": freq, "PhaseOffset": 0.0, "MixerType": x.MIXER_TYPE_FINE,
                           "MixerMode": x.MIXER_MODE_R2C if typ == 0 else x.MIXER_MODE_C2R}
        b.ResetNCOPhase(); b.UpdateEvent(x.EVENT_MIXER)

    # ---- init_drivers() --------------------------------------------------
    def init(self, skip_clocks):
        import xrfdc
        self.xrfdc = xrfdc
        if not skip_clocks:
            import xrfclk
            xrfclk.set_ref_clks(lmk_freq=245.76, lmx_freq=491.52)
            time.sleep(0.5); log("clocks: LMK 245.76 / LMX 491.52 set")
        for typ, tile in ((0, 2), (1, 0)):               # conv_init: ADC tile 2, DAC tile 0
            t, b = self.block(typ, tile, 0)
            t.Reset(); time.sleep(0.2)
            log(("ADC" if typ == 0 else "DAC"), "tile", tile, "status", dict(b.BlockStatus))
        # RFSoC4x2: one real DAC (B) and one real ADC (B).  The design default mixer mode reads back C2C, which
        # is wrong here; Codex's approved v46 coax runtime uses DAC C2R / ADC R2C, DAC VOP 2250 uA, ADC DSA 0 dB.
        dac = self.rf.dac_tiles[0].blocks[0]; adc = self.rf.adc_tiles[2].blocks[0]
        adc.DSA = {"DisableRTS": 1, "Attenuation": 0.0}
        dac.SetDACVOP(2250)
        for typ, tile, freq in ((1, 0, 400.0), (0, 2, -400.0)):   # NCO_init
            self.set_mixer(typ, tile, 0, freq)
        self.rf.dac_tiles[0].SetupFIFO(True); self.rf.adc_tiles[2].SetupFIFO(True)
        log("DAC B mixer", dict(dac.MixerSettings), "| ADC B mixer", dict(adc.MixerSettings), "| ADC DSA", dict(adc.DSA))
        for g in self.gpio:
            g.write(0)
        for base, n in ((TX_XBAR, 6), (RX_XBAR, 10)):    # tx/rx_xbar_init: disable all MI ports
            self.set_switch(base, [None] * n)
        self.w(MODE_GPIO + 4, 0); self.w(MODE_GPIO + 0xC, 0xFFFFFFFF)
        self.w(MODE_GPIO, 0x4); time.sleep(0.001); self.w(MODE_GPIO, 0x0)   # production mode, clear status
        log("router status 0x%08x" % self.r(MODE_GPIO + 8))
        log("HELIX_NLSG_CONTROL_READY")

    # ---- commands --------------------------------------------------------
    def cmd_initplatform(self, a):
        self.gpio[LBM_START].write(0)
        self.gpio[HARD_RESET_TX].write(1); self.gpio[HARD_RESET_RX].write(1); time.sleep(1e-5)
        self.gpio[HARD_RESET_TX].write(0); self.gpio[HARD_RESET_RX].write(0)
        self.reply(struct.unpack("<I", b"1.0\0")[0])

    def cmd_setncofreq(self, a):
        typ, tile, blk, freq = a[0], a[1], a[2], a[3]
        self.set_mixer(typ, tile, blk, freq)

    def cmd_setnyquistzone(self, a):
        self.block(a[0], a[1], a[2])[1].NyquistZone = a[3]

    def cmd_setqmcsettings(self, a):
        t, b = self.block(a[0], a[1], a[2])
        q = dict(b.QMCSettings)
        q.update(EventSource=self.xrfdc.EVNT_SRC_TILE, GainCorrectionFactor=a[4], OffsetCorrectionFactor=-5,
                 EnableGain=a[3], EnablePhase=0)
        b.QMCSettings = q; b.UpdateEvent(self.xrfdc.EVENT_QMC)

    def cmd_writereg(self, a):
        self.w(a[0], a[1])

    def cmd_readreg(self, a):
        self.reply(self.r(a[0]))

    def cmd_setfreezecal(self, a):
        for tile in self.rf.adc_tiles:
            for b in tile.blocks:
                try:
                    b.CalFreeze = {"CalFrozen": 0, "DisableFreezePin": 0, "FreezeCalibration": a[0]}
                except Exception as e:                   # disabled blocks raise; the original ignores per-block state
                    if self.verbose: log("CalFreeze skipped:", e)

    def cmd_triggertx(self, a):
        self.reply(UDP_CMD_ACK)
        self.w(SM, 0); time.sleep(1e-6); self.w(SM + 0x18, a[0] // 8); self.w(SM, 1 << 30)

    def cmd_triggerrx(self, a):
        ce, en, cfo, npk = a[1] & 1, a[2] & 1, a[3] & 1, a[4]
        msg = (1 | ce << 5 | en << 6 | cfo << 8 | npk << 12) if (ce or en or cfo) else 0
        self.w(PKTZ, msg); self.w(SM, 0); time.sleep(1e-6); self.w(SM + 0xC, a[0] // 8)
        self.reply(UDP_CMD_ACK); self.w(SM, 1 << 31)

    def cmd_loadssb(self, a):
        n = a[0]
        if n % 16 or n > FIFO_SIZE:
            self.reply(UDP_CMD_FAILED); raise ValueError(f"loadSSB bad size {n}")
        self.reply(UDP_CMD_ACK); time.sleep(0.001)
        data = b""
        while len(data) < n:
            p = self.recv_packet(timeout=2.0)
            if p is None:
                self.reply(UDP_CMD_FAILED); raise RuntimeError(f"loadSSB got {len(data)} of {n} bytes")
            data += p
        buf = self.allocate(shape=(n,), dtype=np.uint8)
        buf[:] = np.frombuffer(data[:n], dtype=np.uint8); buf.flush()
        g = self.gpio
        g[LBM_START].write(0); g[LBM_LOOPBACK].write(0); self.pulse(LBM_RST); g[LBM_LOAD_DATA].write(1)
        self.tdma.sendchannel.transfer(buf)
        t0 = time.monotonic()
        while not self.tdma.sendchannel.idle:
            if time.monotonic() - t0 > 1.0:
                self.reply(UDP_CMD_FAILED); raise RuntimeError("SSB DMA timeout, SR=0x%08x" % self.tdma.mmio.read(4))
        time.sleep(0.001)
        g[LBM_LOAD_DATA].write(0); g[LBM_LOOPBACK].write(1); g[LBM_START].write(1)
        buf.freebuffer()

    def cmd_changetxsplit(self, a):
        self.set_switch(TX_XBAR, TX_SPLITS[a[0]]); self.pulse(HARD_RESET_TX, 2)

    def cmd_changerxsplit(self, a):
        self.set_switch(RX_XBAR, RX_SPLITS[a[0]]); self.pulse(HARD_RESET_RX, 2)

    def cmd_enablerx(self, a):
        self.gpio[ENABLE_RECV].write(1 if a[0] else 0)
        if not a[0]:
            self.pulse(HARD_RESET_RX)

    def cmd_configudpserver(self, a):
        pass

    def execute(self, line):
        tok = line.replace(",", " ").split()
        if not tok:
            return
        name = tok[0].lower()
        if name not in ARGT:
            log("UNDEFINED command:", line[:80]); return
        types, args = ARGT[name], []
        if len(tok) - 1 < len(types):
            log("WRONG ARG COUNT:", line[:80]); return
        for t, s in zip(types, tok[1:]):
            args.append(float(s) if t == "d" else int(s, 0))
        if self.verbose or name not in ("writereg", "readreg"):
            log("cmd", name, args)
        getattr(self, "cmd_" + name)(args)

    def serve(self, seconds):
        stop = []
        for s in (signal.SIGINT, signal.SIGTERM):
            signal.signal(s, lambda *_: stop.append(1))
        t_end = time.monotonic() + seconds if seconds else None
        n = 0
        while not stop and (t_end is None or time.monotonic() < t_end):
            p = self.recv_packet(timeout=0.2, stop=lambda: bool(stop))
            if p is None:
                continue
            n += 1
            try:
                self.execute(p.split(b"\0", 1)[0].decode("ascii", "replace"))
            except Exception as e:
                log("COMMAND ERROR:", repr(e), "| packet:", p[:60])
        self.cdma.mmio.write(0x00, 0x4)                  # soft-reset the control DMA: no armed S2MM left pointing at freed memory
        log("CONTROL_STOPPED commands=%d" % n)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("bitfile")
    ap.add_argument("--skip-clocks", action="store_true", help="LMK/LMX already programmed")
    ap.add_argument("--skip-init", action="store_true", help="only serve commands (no RFDC/GPIO/switch init)")
    ap.add_argument("--seconds", type=float, default=0)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()
    s = Service(a.bitfile, a.verbose)
    if not a.skip_init:
        s.init(a.skip_clocks)
    else:
        import xrfdc; s.xrfdc = xrfdc
    s.serve(a.seconds)


if __name__ == "__main__":
    sys.exit(main())
