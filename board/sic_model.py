"""Bit-exact golden model of rtl/sic_top.sv (sic_ref_align + sic_cfir2x + sic_monitor)."""
import numpy as np

REF_CONST = 10          # samples of fixed delay in sic_ref_align (dac_tdata -> x), plus cfg_delay
CW, CF = 18, 15
CMAX, CMIN = (1 << (CW - 1)) - 1, -(1 << (CW - 1))


def latency(taps):
    """Clocks from an input beat to the matching output beat."""
    return (int(np.ceil(np.log2(taps))) if taps > 1 else 0) + 5


def sat16(v):
    return np.clip(v, -32768, 32767)


def sic(y, x, h_beat, en_beat, delay):
    """y, x: int64 arrays (N, 2) = [re, im] per sample, N even.
    h_beat: int64 (N/2, TAPS, 2) coefficient set in force for each beat.
    en_beat: (N/2,) 0/1. delay: cfg_delay register value.
    Returns e (N, 2), clip (N, 2) as 0/1."""
    y = np.asarray(y, dtype=np.int64); x = np.asarray(x, dtype=np.int64)
    n = np.arange(len(y)); b = n // 2
    taps = h_beat.shape[1]
    accr = np.zeros(len(y), dtype=np.int64); acci = np.zeros(len(y), dtype=np.int64)
    for k in range(taps):
        idx = n - k - delay - REF_CONST
        ok = idx >= 0
        xr = np.where(ok, x[np.maximum(idx, 0), 0], 0)
        xi = np.where(ok, x[np.maximum(idx, 0), 1], 0)
        hr = h_beat[b, k, 0]; hi = h_beat[b, k, 1]
        accr += hr * xr - hi * xi
        acci += hr * xi + hi * xr
    half = 1 << (CF - 1)
    er_raw = (accr + half) >> CF; ei_raw = (acci + half) >> CF
    est = np.stack([sat16(er_raw), sat16(ei_raw)], axis=1)
    est_clip = np.stack([er_raw != est[:, 0], ei_raw != est[:, 1]], axis=1)
    d = y - est
    e_on = sat16(d)
    clip_on = (d != e_on) | est_clip
    en = np.asarray(en_beat, dtype=bool)[b][:, None]
    return np.where(en, e_on, y), np.where(en, clip_on, False).astype(np.int64), est


def quantize(h):
    """Complex float taps -> int (TAPS, 2) in Q(CF), saturated to CW bits."""
    q = np.stack([np.round(h.real * (1 << CF)), np.round(h.imag * (1 << CF))], axis=1).astype(np.int64)
    return np.clip(q, CMIN, CMAX)
