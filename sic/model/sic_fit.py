"""Least-squares coefficient and delay estimation from paired TX-reference / RX captures.
Works on any complex arrays, so the same code serves simulation now and board captures later."""
import numpy as np
from sic_model import REF_CONST, quantize


def find_delay(x, y, max_delay=4096):
    """Integer lag (samples) by which y trails x, from the cross-correlation peak."""
    n = 1 << int(np.ceil(np.log2(len(x) + max_delay)))
    c = np.fft.ifft(np.fft.fft(y, n) * np.conj(np.fft.fft(x, n)))
    return int(np.argmax(np.abs(c[:max_delay])))


def fit(x, y, taps=4, pre=1, ridge=1e-6, max_delay=4096):
    """Returns (cfg_delay, h_float, h_q15). Tap `pre` sits on the correlation peak."""
    x = np.asarray(x, dtype=np.complex128); y = np.asarray(y, dtype=np.complex128)
    lag = find_delay(x, y, max_delay)
    d0 = lag - pre                       # delay of tap 0
    if d0 - REF_CONST < 0:
        raise ValueError(f"loop delay {lag} samples is shorter than the aligner minimum")
    n = np.arange(d0 + taps, len(x))
    A = np.stack([x[n - d0 - k] for k in range(taps)], axis=1)
    G = A.conj().T @ A
    G += ridge * np.trace(G).real / taps * np.eye(taps)
    h = np.linalg.solve(G, A.conj().T @ y[n])
    return d0 - REF_CONST, h, quantize(h)
