// Claude 2026-11-11: parameter derivation for the FPGA TX split-6 front end (no dependencies; shared with the HLS testbench).
#pragma once
#include <cstdint>
namespace split6 {
struct enc_params { uint32_t A, C, cb_crc, Kp, Zc, iLS, fill_col, fill_idx, Qm, n_floor, eq_floor, eq_ceil, rows_floor, rows_ceil, total_words; };
inline uint32_t rows_for(uint32_t E, uint32_t F, uint32_t Zc) { uint32_t N = 66 * Zc, need = E + F; if (need > N) need = N; uint32_t last_col = 2 + (need + Zc - 1) / Zc; uint32_t r = last_col - 22; return r < 4 ? 4 : (r > 46 ? 46 : r); }
// A = transport block bits, G = rate-matched bits per slot (num_sch * Qm), flush_words = zero words appended for HELIX (3)
inline bool derive(uint32_t A, uint32_t G, uint32_t Qm, uint32_t flush_words, enc_params& p) {
    if (A <= 3824 || 4ull * A <= G) return false;                 // BG2 cases are not implemented in the FPGA block
    const uint32_t Kcb = 8448, B = A + 24; p.A = A; p.Qm = Qm;
    if (B > Kcb) { p.C = (B + (Kcb - 24) - 1) / (Kcb - 24); if ((B + p.C * 24) % p.C) return false; p.Kp = (B + p.C * 24) / p.C; p.cb_crc = 1; }
    else { p.C = 1; p.Kp = B; p.cb_crc = 0; }
    static const uint32_t a[8] = {2, 3, 5, 7, 9, 11, 13, 15}; p.Zc = 0;
    for (int i = 0; i < 8; i++) for (uint32_t z = a[i]; z <= 384; z *= 2) if (22 * z >= p.Kp && (!p.Zc || z < p.Zc)) { p.Zc = z; p.iLS = i; }
    if (!p.Zc || p.C > 100 || G % Qm) return false;
    p.fill_col = p.Kp / p.Zc; p.fill_idx = p.Kp % p.Zc;
    const uint32_t Gp = G / Qm, F = 22 * p.Zc - p.Kp;
    p.n_floor = p.C - (Gp % p.C); p.eq_floor = Gp / p.C; p.eq_ceil = (Gp + p.C - 1) / p.C;
    p.rows_floor = rows_for(p.eq_floor * Qm, F, p.Zc); p.rows_ceil = rows_for(p.eq_ceil * Qm, F, p.Zc);
    p.total_words = (G + 63) / 64 + flush_words; return true;
}
}
