// Claude 2026-11-11: host-side setup of the FPGA TX split-6 front end (ldpc_tx_enc, helix_nlsg_s6.bit, 0xB0011000).
// Call after radio.control->configure_radio(rx_split, SPLIT_6, ...): the TX reset in changeTXsplit clears these registers.
#pragma once
#include "../include/helix.h"
#include "../include/defines.h"
#include <cstdint>
#include <iostream>
#define TX_LDPC_ENC_ADDR 0xB0011000
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
inline bool enable(helix::helix& radio, uint32_t A, uint32_t G, uint32_t Qm, uint32_t flush_words = 3) {
    enc_params p; if (!derive(A, G, Qm, flush_words, p)) { std::cerr << "split6: unsupported TB/G combination A=" << A << " G=" << G << std::endl; return false; }
    const uint32_t b = TX_LDPC_ENC_ADDR;
    radio.cmd->writeReg(b + 0x10, 0);
    radio.cmd->writeReg(b + 0x18, p.A); radio.cmd->writeReg(b + 0x20, p.C); radio.cmd->writeReg(b + 0x28, p.cb_crc); radio.cmd->writeReg(b + 0x30, p.Kp);
    radio.cmd->writeReg(b + 0x38, p.Zc); radio.cmd->writeReg(b + 0x40, p.iLS); radio.cmd->writeReg(b + 0x48, p.fill_col); radio.cmd->writeReg(b + 0x50, p.fill_idx);
    radio.cmd->writeReg(b + 0x58, p.Qm); radio.cmd->writeReg(b + 0x60, p.n_floor); radio.cmd->writeReg(b + 0x68, p.eq_floor); radio.cmd->writeReg(b + 0x70, p.eq_ceil);
    radio.cmd->writeReg(b + 0x78, p.rows_floor); radio.cmd->writeReg(b + 0x80, p.rows_ceil); radio.cmd->writeReg(b + 0x88, p.total_words);
    radio.cmd->writeReg(b + 0x10, 1);
    if (radio.cmd->readReg(b + 0x10) != 1 || radio.cmd->readReg(b + 0x38) != p.Zc) { std::cerr << "split6: encoder registers did not read back (is helix_nlsg_s6.bit loaded?)" << std::endl; return false; }
    std::cout << "split6 encoder on: A=" << p.A << " C=" << p.C << " K'=" << p.Kp << " Zc=" << p.Zc << " iLS=" << p.iLS << " E=" << p.eq_floor * Qm << "/" << p.eq_ceil * Qm << " rows=" << p.rows_floor << "/" << p.rows_ceil << " words=" << p.total_words << std::endl;
    return true;
}
inline uint32_t packets_done(helix::helix& radio) { return radio.cmd->readReg(TX_LDPC_ENC_ADDR + 0x90); }
}
