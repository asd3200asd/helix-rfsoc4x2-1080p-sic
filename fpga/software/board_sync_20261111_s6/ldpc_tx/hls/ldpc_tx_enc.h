// Claude 2026-11-11: HELIX TX "split 6" front end (Vitis HLS).  See ldpc_tx_enc.cpp.
#pragma once
#include <stdint.h>
#include "ap_axi_sdata.h"
#include "hls_stream.h"
typedef ap_axiu<64, 0, 0, 0> axis64_t;
void ldpc_tx_enc(hls::stream<axis64_t>& s_axis, hls::stream<axis64_t>& m_axis,
                 uint32_t enable, uint32_t tb_bits, uint32_t num_cb, uint32_t cb_crc, uint32_t kprime, uint32_t zc, uint32_t ils,
                 uint32_t fill_col, uint32_t fill_idx, uint32_t qm, uint32_t n_floor, uint32_t eq_floor, uint32_t eq_ceil,
                 uint32_t rows_floor, uint32_t rows_ceil, uint32_t total_words, uint32_t* packets_done);
