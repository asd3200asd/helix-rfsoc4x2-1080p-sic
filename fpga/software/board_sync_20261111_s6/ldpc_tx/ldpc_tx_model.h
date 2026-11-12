// Claude 2026-11-11: NR DL-SCH TX "split 6" front end, plain-C++ reference model (BG1 only for now).
// transport block bytes -> CRC24A -> segmentation + CRC24B + fillers -> LDPC encode (BG1) -> rate matching (rv0, no LBRM)
// -> bit interleaving -> concatenation -> bits packed LSB-first (the HELIX Split-7.3 input format).
#pragma once
#include <cstdint>
#include "bg1_shift.h"
namespace ldpctx {
const int ZMAX = 384, NCOL = 68, NROW = 46, KB = 22, MAXC = 8, MAXG = 1 << 17;
struct Params { int A, C, Kp, K, Zc, iLS, G, Qm; };          // Kp = K' (bits per code block incl. CB CRC, before fillers)
inline int lifting_set(int Zc) { static const int a[8] = {2,3,5,7,9,11,13,15}; for (int i = 0; i < 8; i++) for (int z = a[i]; z <= 384; z *= 2) if (z == Zc) return i; return -1; }
// derive parameters for BG1 from transport block size (bits), total rate-matched bits G and modulation order
inline bool derive(int A, int G, int Qm, Params& p) {
    if (A <= 3824) return false;                              // CRC16 case not implemented
    int B = A + 24; const int Kcb = 8448;
    p.A = A; p.G = G; p.Qm = Qm;
    if (B > Kcb) { p.C = (B + (Kcb - 24) - 1) / (Kcb - 24); p.Kp = (B + p.C * 24) / p.C; if ((B + p.C * 24) % p.C) return false; }
    else { p.C = 1; p.Kp = B; }
    static const int a[8] = {2,3,5,7,9,11,13,15}; int best = 0;
    for (int i = 0; i < 8; i++) for (int z = a[i]; z <= 384; z *= 2) if (KB * z >= p.Kp && (!best || z < best)) best = z;
    if (!best || p.C > MAXC || G > MAXG) return false;
    p.Zc = best; p.K = KB * best; p.iLS = lifting_set(best); return true;
}
inline uint32_t crc24_step(uint32_t crc, int bit, uint32_t poly) { int fb = ((crc >> 23) & 1) ^ bit; crc = (crc << 1) & 0xFFFFFF; return fb ? crc ^ poly : crc; }
// tb: A bits as bytes, LSB-first inside each byte.  out_bits: G bits (0/1).  returns false if unsupported.
inline bool encode(const uint8_t* tb, const Params& p, uint8_t* out_bits) {
    static uint8_t w[NCOL][ZMAX]; uint8_t lam[4][ZMAX];
    const int Zc = p.Zc, N = 66 * Zc; const int16_t (*bg)[NCOL] = BG1_SHIFT[p.iLS];
    if (bg[0][22] != bg[3][22] || bg[2][22] != -1) return false;
    uint32_t crcA = 0; for (int i = 0; i < p.A; i++) crcA = crc24_step(crcA, (tb[i >> 3] >> (i & 7)) & 1, 0x864CFB);
    int src = 0, gout = 0; const int B = p.A + 24, data_per_cb = (p.C > 1) ? p.Kp - 24 : p.Kp;
    for (int r = 0; r < p.C; r++) {
        uint32_t crcB = 0;
        for (int k = 0; k < p.K; k++) {                       // info part incl. CRCs and zero fillers
            int bit = 0;
            if (k < data_per_cb) { bit = (src < p.A) ? (tb[src >> 3] >> (src & 7)) & 1 : (src < B ? (crcA >> (23 - (src - p.A))) & 1 : 0); src++; crcB = crc24_step(crcB, bit, 0x800063); }
            else if (k < p.Kp) bit = (crcB >> (23 - (k - data_per_cb))) & 1;
            w[k / Zc][k % Zc] = bit;
        }
        for (int i = 0; i < 4; i++) for (int z = 0; z < Zc; z++) lam[i][z] = 0;
        for (int i = 0; i < 4; i++) for (int j = 0; j < KB; j++) if (bg[i][j] >= 0) { int s = bg[i][j] % Zc; for (int z = 0; z < Zc; z++) lam[i][z] ^= w[j][(z + s) % Zc]; }
        int a0 = bg[0][22] % Zc, a1 = bg[1][22] % Zc;
        for (int z = 0; z < Zc; z++) w[22][(z + a1) % Zc] = lam[0][z] ^ lam[1][z] ^ lam[2][z] ^ lam[3][z];     // rot(p1,a1) = sum(lambda)
        for (int z = 0; z < Zc; z++) w[23][z] = lam[0][z] ^ w[22][(z + a0) % Zc];
        for (int z = 0; z < Zc; z++) w[24][z] = lam[1][z] ^ w[22][(z + a1) % Zc] ^ w[23][z];
        for (int z = 0; z < Zc; z++) w[25][z] = lam[3][z] ^ w[22][(z + a0) % Zc];
        // rate matching: E_r bits from d = columns 2..67, skipping fillers, wrapping; only the needed extension rows are computed
        int Gp = p.G / p.Qm, E = (r <= p.C - (Gp % p.C) - 1) ? p.Qm * (Gp / p.C) : p.Qm * ((Gp + p.C - 1) / p.C);
        int fill_lo = p.Kp - 2 * Zc, fill_hi = p.K - 2 * Zc, need = E + (fill_hi - fill_lo); if (need > N) need = N;
        int last_col = 2 + (need + Zc - 1) / Zc;              // exclusive
        for (int i = 4; i < NROW && 22 + i < last_col; i++) {
            for (int z = 0; z < Zc; z++) w[22 + i][z] = 0;
            for (int j = 0; j < 26; j++) if (bg[i][j] >= 0) { int s = bg[i][j] % Zc; for (int z = 0; z < Zc; z++) w[22 + i][z] ^= w[j][(z + s) % Zc]; }
        }
        static uint8_t e[MAXG]; int n = 0, k = 0;
        while (n < E) { if (!(k >= fill_lo && k < fill_hi)) e[n++] = w[2 + k / Zc][k % Zc]; k++; if (k == N) k = 0; }
        for (int j = 0; j < E / p.Qm; j++) for (int i = 0; i < p.Qm; i++) out_bits[gout + i + j * p.Qm] = e[i * (E / p.Qm) + j];
        gout += E;
    }
    return gout == p.G;
}
}
