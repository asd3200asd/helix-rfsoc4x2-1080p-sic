// Claude 2026-11-11: host-side setup of the FPGA TX split-6 front end (ldpc_tx_enc, helix_nlsg_s6.bit, 0xB0011000).
// Call after radio.control->configure_radio(rx_split, SPLIT_6, ...): the TX reset in changeTXsplit clears these registers.
#pragma once
#include "../include/helix.h"
#include "../include/defines.h"
#include <cstdint>
#include <iostream>
#include "split6_params.h"
#define TX_LDPC_ENC_ADDR 0xB0011000
namespace split6 {
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
