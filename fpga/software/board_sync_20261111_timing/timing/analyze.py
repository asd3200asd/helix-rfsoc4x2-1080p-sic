# Claude 2026-11-11: merge host CSV + hardware probe + control-service + bridge timestamps (all CLOCK_MONOTONIC ns).
# usage: python3 analyze.py <run_dir>
import sys, numpy as np, bisect
D = sys.argv[1]
def load(fn, kinds=None):
    ev = {}
    for l in open(f"{D}/{fn}"):
        if l.startswith("#"): continue
        k, v, *t = l.split()
        if kinds is None: ev.setdefault((k, int(v)), []).append(int(t[0]))
        else: ev.setdefault(k, []).append([int(v), int(t[0])])
    return ev
probe = load("probe.log"); bridge = load("bridge.log"); ctrl = load("ctrl.log", kinds=True)
def first_in(lst, lo, hi):
    i = bisect.bisect_left(lst, lo); return lst[i] if i < len(lst) and lst[i] <= hi else None
def stat(name, xs, unit="us"):
    xs = np.array([x for x in xs if x is not None], float) / 1e3
    if xs.size == 0: print(f"  {name:58s} n=0"); return None
    print(f"  {name:58s} n={xs.size:4d} median={np.median(xs):9.1f} p10={np.percentile(xs,10):9.1f} p90={np.percentile(xs,90):9.1f} max={xs.max():9.1f} {unit}"); return float(np.median(xs))
E = [t for (k, v), ts in probe.items() if k == "E" for t in ts]; E.sort()
PT = sorted(probe.get(("T", 1394), [])); PR6 = sorted(probe.get(("R", 1450), []))
PRall = sorted(t for (k, v), ts in probe.items() if k == "R" for t in ts)   # probe could not read S2MM lengths: frames are identified by order
PR8 = PRall
BT = sorted(bridge.get(("T", 1394), [])); BR6 = sorted(bridge.get(("R", 1450), []))
ctx = sorted(ctrl.get("triggerTX", [])); crx = sorted(ctrl.get("triggerRX", []))
def ctl(lst, lo, hi):
    i = bisect.bisect_left(lst, [lo, 0]); return lst[i] if i < len(lst) and lst[i][0] <= hi else None
for fn, title in (("normal_s6.csv", "MODE 0  triggerTX -> data -> triggerRX -> receive (RX split 6)"), ("armed_s6.csv", "MODE 1  triggerRX first, then triggerTX -> data -> receive (RX split 6)"), ("armed_s8.csv", "MODE 1  RX split 8 (raw IQ, 123792 B)")):
    rows = [list(map(int, l.split(","))) for l in open(f"{D}/{fn}")]
    armed = "armed" in fn; s8 = "s8" in fn
    print(f"\n=== {title}: {len(rows)} packets, ok={sum(r[6] for r in rows)}")
    c = {k: [] for k in "total txtrig data rxtrig rxwait tap_tx enc enc_wait hw_chain hw8_first hw8_last br_rx host_rx ctl_tx_lat ctl_tx_exec ctl_rx_lat ctl_rx_exec trig_to_data rx_trig_to_out dec_wait".split()}
    for i, t0, ta, tb, tc, td, ok in rows:
        if not ok: continue
        c["total"].append(td - t0)
        if armed: c["rxtrig"].append(ta - t0); c["txtrig"].append(tb - ta); c["data"].append(tc - tb); c["rxwait"].append(td - tc)
        else: c["txtrig"].append(tb - t0); c["data"].append(tc - tb); c["rxtrig"].append(ta - tc); c["rxwait"].append(td - ta)
        tT = first_in(PT, tb - 50000, td); tE = first_in(E, tb, td + 2000000)
        x = ctl(ctx, t0 - 100000, tb); r = ctl(crx, t0 - 100000, td)
        if x: c["ctl_tx_exec"].append(x[1] - x[0])
        if r: c["ctl_rx_exec"].append(r[1] - r[0])
        if tT: c["tap_tx"].append(tT - tb)                     # host sendto() start -> frame leaves the DMA
        if tT and tE and x:
            start = max(tT, x[1]); c["enc"].append(tE - start); c["enc_wait"].append(start - tT)
        if s8:
            rr = [t for t in PR8[bisect.bisect_left(PR8, tb):bisect.bisect_right(PR8, td + 1000000)]]
            rr = [t for t in rr if tE and t > tE]
            if tE and rr: c["hw8_first"].append(rr[0] - tE); c["hw8_last"].append(rr[-1] - tE)
        else:
            bR = first_in(BR6, tb, td + 1000000)
            if armed: tR = first_in(PRall, tE + 1, td) if tE else None            # nothing else comes back after the encoder finished
            else:
                j = bisect.bisect_right(PRall, td) - 1; tR = PRall[j] if j >= 0 and tE and PRall[j] > tE else None   # last frame before the program got its data
            if tE and tR:
                if armed: c["hw_chain"].append(tR - tE)
                elif r: c["rx_trig_to_out"].append(tR - r[1]); c["dec_wait"].append(r[1] - tE)
            if tR and bR: c["br_rx"].append(bR - tR)
            if bR: c["host_rx"].append(td - bR)
    stat("host: total per packet (start -> RX data in program)", c["total"])
    stat("host: triggerTX command round trip (send -> ACK)", c["txtrig"])
    stat("host: sendto() of the TB datagram", c["data"])
    stat("host: triggerRX command round trip (send -> ACK)", c["rxtrig"])
    stat("host: wait for RX data after last step", c["rxwait"])
    stat("PS: control service executes triggerTX (rx -> SM written)", c["ctl_tx_exec"])
    stat("PS: control service executes triggerRX", c["ctl_rx_exec"])
    stat("PS: host sendto -> TB frame leaves SG DMA (TAP + bridge)", c["tap_tx"])
    stat("FPGA: TB at DMA -> encoder start (waiting for SM trigger)", c["enc_wait"])
    stat("FPGA: LDPC encoder (start -> packets_done++)", c["enc"])
    if s8:
        stat("FPGA: encoder done -> first raw-IQ frame out of S2MM", c["hw8_first"]); stat("FPGA: encoder done -> last raw-IQ frame out of S2MM", c["hw8_last"])
    else:
        stat("FPGA: encoder done -> decoded packet out of S2MM (armed)", c["hw_chain"])
        stat("FPGA/PS: encoder done -> SM RX trigger written (mode 0)", c["dec_wait"])
        stat("FPGA: SM RX trigger written -> decoded packet out of S2MM", c["rx_trig_to_out"])
        stat("PS: packet at S2MM -> bridge notices it (polling)", c["br_rx"])
        stat("PS: packet at S2MM -> host program has the data", [a + b for a, b in zip(c["br_rx"], c["host_rx"])] if len(c["br_rx"]) == len(c["host_rx"]) else [])
print("\nprobe:", open(f"{D}/probe.log").readline().strip(), "| frames seen by probe:", {f"{k}{v}": len(t) for (k, v), t in sorted(probe.items()) if len(t) > 20})
