// Claude 2026-11-11: HELIX TX "split 6" front end for RFSoC4x2 (xczu48dr), Vitis HLS 2024.1, target clock 245.76 MHz.
// enable=0: every word is passed through unchanged (original Split-7.3 behaviour).
// enable=1: one AXIS packet = one transport block (bytes, bit 0 of byte 0 first; padding after tb_bits is ignored).
//   TB -> CRC24A -> segmentation (+CRC24B, zero fillers) -> NR LDPC BG1 encode -> rate matching (rv0, no LBRM)
//   -> bit interleaving -> code block concatenation -> 64-bit words, bit n of the stream in bit (n%64) of word n/64,
//   followed by zero words up to total_words (HELIX needs 3 flush words); TLAST on the last word.
// Software supplies the derived parameters (control service: settxencoder); only BG1 with TB > 3824 bits is supported.
// Bit-serial: about (nonzeros used + 5) * Zc clock cycles per code block.
// Build with -DLDPC_PLAIN_TYPES and the shim headers for a plain g++ functional check.
#include "ldpc_tx_enc.h"
#include "bg1_sparse.h"
#ifdef LDPC_PLAIN_TYPES
typedef unsigned zidx_t; typedef unsigned col_t; typedef unsigned cnt_t; typedef uint32_t crc_t; typedef unsigned nz_t; typedef unsigned small_t;
#else
#include "ap_int.h"
typedef ap_uint<9> zidx_t; typedef ap_uint<7> col_t; typedef ap_uint<20> cnt_t; typedef ap_uint<24> crc_t; typedef ap_uint<10> nz_t; typedef ap_uint<7> small_t;
#endif
#define ADDR(c, i) ((((unsigned)(c)) << 9) | ((unsigned)(i)))      // codeword memory address: column stride 512
static zidx_t modz(zidx_t v_in, zidx_t zcv) {                       // v mod zc (sequential: 9 compare/subtract steps)
#pragma HLS INLINE off
    unsigned v = v_in;
    for (int k = 8; k >= 0; k--) { unsigned t = ((unsigned)zcv) << k; if (v >= t) v -= t; }
    return (zidx_t)v;
}
static void emit(hls::stream<axis64_t>& m_axis, uint64_t d, bool last) {
#pragma HLS INLINE
    axis64_t y; y.data = d; y.keep = 0xFF; y.strb = 0xFF; y.last = last; m_axis.write(y);
}
void ldpc_tx_enc(hls::stream<axis64_t>& s_axis, hls::stream<axis64_t>& m_axis,
                 uint32_t enable, uint32_t tb_bits, uint32_t num_cb, uint32_t cb_crc, uint32_t kprime, uint32_t zc, uint32_t ils,
                 uint32_t fill_col, uint32_t fill_idx, uint32_t qm, uint32_t n_floor, uint32_t eq_floor, uint32_t eq_ceil,
                 uint32_t rows_floor, uint32_t rows_ceil, uint32_t total_words, uint32_t* packets_done) {
#pragma HLS INTERFACE axis port=s_axis
#pragma HLS INTERFACE axis port=m_axis
#pragma HLS INTERFACE s_axilite port=enable bundle=ctrl
#pragma HLS INTERFACE s_axilite port=tb_bits bundle=ctrl
#pragma HLS INTERFACE s_axilite port=num_cb bundle=ctrl
#pragma HLS INTERFACE s_axilite port=cb_crc bundle=ctrl
#pragma HLS INTERFACE s_axilite port=kprime bundle=ctrl
#pragma HLS INTERFACE s_axilite port=zc bundle=ctrl
#pragma HLS INTERFACE s_axilite port=ils bundle=ctrl
#pragma HLS INTERFACE s_axilite port=fill_col bundle=ctrl
#pragma HLS INTERFACE s_axilite port=fill_idx bundle=ctrl
#pragma HLS INTERFACE s_axilite port=qm bundle=ctrl
#pragma HLS INTERFACE s_axilite port=n_floor bundle=ctrl
#pragma HLS INTERFACE s_axilite port=eq_floor bundle=ctrl
#pragma HLS INTERFACE s_axilite port=eq_ceil bundle=ctrl
#pragma HLS INTERFACE s_axilite port=rows_floor bundle=ctrl
#pragma HLS INTERFACE s_axilite port=rows_ceil bundle=ctrl
#pragma HLS INTERFACE s_axilite port=total_words bundle=ctrl
#pragma HLS INTERFACE s_axilite port=packets_done bundle=ctrl
#pragma HLS INTERFACE ap_ctrl_none port=return
    static bool w[68 << 9];                    // codeword memory
    static uint32_t done_count = 0;
    bool lam[4][512];
#pragma HLS ARRAY_PARTITION variable=lam dim=1 complete
    bool p1a[512], p1b[512], acc[512];

    axis64_t x = s_axis.read();
    if (!(enable & 1)) {                       // ---- bypass
        m_axis.write(x);
        bool l = x.last;
        BYPASS: while (!l) {
#pragma HLS PIPELINE II=1
            x = s_axis.read(); m_axis.write(x); l = x.last;
        }
        return;
    }
    const zidx_t Zc = (zidx_t)zc, zm1 = (zidx_t)(zc - 1);
    const cnt_t A = (cnt_t)tb_bits, Kp = (cnt_t)kprime, K = (cnt_t)(22u * zc);
    const cnt_t dpc = (cnt_t)(cb_crc ? kprime - 24 : kprime);
    const small_t Qm = (small_t)qm, qm1 = (small_t)(qm - 1), ILS = (small_t)ils;
    const col_t fcol = (col_t)fill_col; const zidx_t fidx = (zidx_t)fill_idx;
    // position whose successor is the first filler bit (fillers end at column 22, index 0)
    const col_t pre_fc = (col_t)(fill_idx == 0 ? fill_col - 1 : fill_col); const zidx_t pre_fi = (zidx_t)(fill_idx == 0 ? zc - 1 : fill_idx - 1);
    const cnt_t tw1 = (cnt_t)(total_words - 1), tw = (cnt_t)total_words;
    uint64_t sh = x.data.to_uint64(); bool in_last = x.last;
    cnt_t src = 0, words_out = 0; crc_t crcA = 0; uint64_t ow = 0; small_t ob = 0; small_t inb = 0;
    const zidx_t a0 = modz((zidx_t)BG1_NZ_V[ILS][BG1_NZ_R0C22], Zc), a1 = modz((zidx_t)BG1_NZ_V[ILS][BG1_NZ_R1C22], Zc);

    CB: for (small_t r = 0; r < (small_t)num_cb; r++) {
        // ---- information bits, CRCs, fillers
        crc_t crcB = 0; col_t col = 0; zidx_t idx = 0;
        FILL: for (cnt_t k = 0; k < K; k++) {
#pragma HLS PIPELINE II=1
            bool bit = 0;
            if (k < dpc) {
                if (src < A) {
                    if (inb == 64) { if (!in_last) { axis64_t y = s_axis.read(); sh = y.data.to_uint64(); in_last = y.last; } else sh = 0; inb = 0; }
                    bit = sh & 1; sh >>= 1; inb++;
                    bool fb = ((crcA >> 23) & 1) ^ bit; crcA = (crc_t)((crcA << 1) & 0xFFFFFF); if (fb) crcA ^= 0x864CFB;
                } else { bit = (crcA >> 23) & 1; crcA = (crc_t)((crcA << 1) & 0xFFFFFF); }
                src++;
                bool fb = ((crcB >> 23) & 1) ^ bit; crcB = (crc_t)((crcB << 1) & 0xFFFFFF); if (fb) crcB ^= 0x800063;
            } else if (k < Kp) { bit = (crcB >> 23) & 1; crcB = (crc_t)((crcB << 1) & 0xFFFFFF); }
            w[ADDR(col, idx)] = bit;
            if (idx == zm1) { idx = 0; col++; } else idx++;
        }
        // ---- core parity (rows 0..3): lambda_i = sum_j rot(c_j, P_ij)
        CORE_ROW: for (int i = 0; i < 4; i++) {
            bool first = true;
            CORE_NZ: for (nz_t nz = BG1_ROW_START[i]; nz < BG1_ROW_START[i + 1]; nz++) {
                const col_t c = BG1_NZ_COL[nz];
                if (c < 22) {
                    zidx_t rd = modz((zidx_t)BG1_NZ_V[ILS][nz], Zc);
                    CORE_Z: for (zidx_t z = 0; ; z++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=lam inter false
                        bool b = w[ADDR(c, rd)];
                        bool old = first ? false : lam[i][z];
                        lam[i][z] = old ^ b;
                        if (rd == zm1) rd = 0; else rd++;
                        if (z == zm1) break;
                    }
                    first = false;
                }
            }
        }
        { zidx_t wr = a1;
          P1: for (zidx_t z = 0; ; z++) {
#pragma HLS PIPELINE II=1
              bool s = lam[0][z] ^ lam[1][z] ^ lam[2][z] ^ lam[3][z];
              p1a[wr] = s; p1b[wr] = s; w[ADDR(22, wr)] = s;
              if (wr == zm1) wr = 0; else wr++;
              if (z == zm1) break;
          } }
        { zidx_t r0 = a0, r1 = a1;
          P2: for (zidx_t z = 0; ; z++) {
#pragma HLS PIPELINE II=1
              w[ADDR(23, z)] = lam[0][z] ^ p1a[r0];
              if (r0 == zm1) r0 = 0; else r0++;
              if (z == zm1) break;
          }
          r0 = a0;
          P3: for (zidx_t z = 0; ; z++) {
#pragma HLS PIPELINE II=1
              w[ADDR(24, z)] = lam[1][z] ^ p1b[r1] ^ lam[0][z] ^ p1a[r0];
              if (r0 == zm1) r0 = 0; else r0++;
              if (r1 == zm1) r1 = 0; else r1++;
              if (z == zm1) break;
          }
          r0 = a0;
          P4: for (zidx_t z = 0; ; z++) {
#pragma HLS PIPELINE II=1
              w[ADDR(25, z)] = lam[3][z] ^ p1a[r0];
              if (r0 == zm1) r0 = 0; else r0++;
              if (z == zm1) break;
          } }
        // ---- extension parity rows actually needed by rate matching
        const bool use_floor = r < (small_t)n_floor;
        const small_t nrows = (small_t)(use_floor ? rows_floor : rows_ceil);
        const cnt_t Eq = (cnt_t)(use_floor ? eq_floor : eq_ceil), eq1 = (cnt_t)(Eq - 1);
        EXT_ROW: for (small_t i = 4; i < nrows; i++) {
            bool first = true;
            EXT_NZ: for (nz_t nz = BG1_ROW_START[i]; nz < BG1_ROW_START[i + 1]; nz++) {
                const col_t c = BG1_NZ_COL[nz];
                if (c < 26) {
                    zidx_t rd = modz((zidx_t)BG1_NZ_V[ILS][nz], Zc);
                    EXT_Z: for (zidx_t z = 0; ; z++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=acc inter false
                        bool b = w[ADDR(c, rd)];
                        bool old = first ? false : acc[z];
                        acc[z] = old ^ b;
                        if (rd == zm1) rd = 0; else rd++;
                        if (z == zm1) break;
                    }
                    first = false;
                }
            }
            const col_t pcol = (col_t)(22 + i);
            EXT_WR: for (zidx_t z = 0; ; z++) {
#pragma HLS PIPELINE II=1
                w[ADDR(pcol, z)] = acc[z];
                if (z == zm1) break;
            }
        }
        // ---- rate matching + bit interleaving: Qm read pointers kept as a rotating queue, pointer i starts at e-index i*E/Qm
        col_t pc[8]; zidx_t pi[8];
#pragma HLS ARRAY_PARTITION variable=pc complete
#pragma HLS ARRAY_PARTITION variable=pi complete
        { col_t c = 2; zidx_t ix = 0; cnt_t cnt = 0; small_t i = 0; pc[0] = 2; pi[0] = 0;
          const cnt_t steps = (cnt_t)((unsigned)qm1 * (unsigned)Eq);
          WALK: for (cnt_t s = 0; s < steps; s++) {
#pragma HLS PIPELINE II=1
              { const bool wrap = (ix == zm1), pre = (c == pre_fc && ix == pre_fi), endc = (c == 67);   // parallel decode of the next position
                if (pre) { c = 22; ix = 0; } else if (wrap) { c = endc ? (col_t)2 : (col_t)(c + 1); ix = 0; } else ix++; }
              if (cnt == eq1) { cnt = 0; i++; pc[i & 7] = c; pi[i & 7] = ix; } else cnt++;
          } }
        { const cnt_t E = (cnt_t)((unsigned)Eq * (unsigned)Qm);
          OUT: for (cnt_t n = 0; n < E; n++) {
#pragma HLS PIPELINE II=1
              col_t c = pc[0]; zidx_t ix = pi[0];
              bool bit = w[ADDR(c, ix)];
              { const bool wrap = (ix == zm1), pre = (c == pre_fc && ix == pre_fi), endc = (c == 67);   // parallel decode of the next position
                if (pre) { c = 22; ix = 0; } else if (wrap) { c = endc ? (col_t)2 : (col_t)(c + 1); ix = 0; } else ix++; }
              for (int j = 0; j < 8; j++) {
#pragma HLS UNROLL
                  if (j == (int)qm1) { pc[j] = c; pi[j] = ix; } else if (j < 7) { pc[j] = pc[j + 1]; pi[j] = pi[j + 1]; }
              }
              ow = (ow >> 1) | ((uint64_t)bit << 63);
              if (ob == 63) { emit(m_axis, ow, words_out == tw1); words_out++; ob = 0; } else ob++;
          } }
    }
    PAD: while (ob != 0) {                     // zero-pad the last partial word
#pragma HLS PIPELINE II=1
        ow >>= 1;
        if (ob == 63) { emit(m_axis, ow, words_out == tw1); words_out++; ob = 0; } else ob++;
    }
    FLUSH: while (words_out < tw) {
#pragma HLS PIPELINE II=1
        emit(m_axis, 0, words_out == tw1); words_out++;
    }
    DRAIN: while (!in_last) {
#pragma HLS PIPELINE II=1
        axis64_t y = s_axis.read(); in_last = y.last;
    }
    done_count++; *packets_done = done_count;
}
