// Claude 2026-11-11: HELIX TX "split 6" front end, version 2 (word-parallel parity), RFSoC4x2, Vitis HLS 2024.1, 245.76 MHz.
// Same interface, registers and bit-exact output as version 1 (ldpc_tx_enc_v1_bitserial.cpp):
// enable=0: pass-through.  enable=1: one AXIS packet = one transport block (bytes, bit 0 of byte 0 first).
//   TB -> CRC24A -> segmentation (+CRC24B, zero fillers) -> NR LDPC BG1 encode -> rate matching (rv0, no LBRM)
//   -> bit interleaving -> concatenation -> 64-bit words (bit n in bit n%64 of word n/64) -> zero words up to total_words.
// What changed: each codeword column is one Zc-bit word (ap_uint<384>); a parity row is one rotate+XOR per non-zero entry
// (one clock each) instead of Zc clocks, and the read pointers of the bit interleaver are computed instead of walked.
// Information fill and output are still one bit per clock: about (K + E + 600) clocks per code block.
#include "ldpc_tx_enc.h"
#include "bg1_sparse.h"
#include "ap_int.h"
typedef ap_uint<384> zv_t; typedef ap_uint<9> zidx_t; typedef ap_uint<7> col_t; typedef ap_uint<20> cnt_t; typedef ap_uint<24> crc_t;
typedef ap_uint<10> nz_t; typedef ap_uint<7> small_t;
static zidx_t modz(zidx_t v_in, zidx_t zcv) {                       // v mod zc, combinational (9 compare/subtract stages)
#pragma HLS INLINE
    ap_uint<10> v = v_in;
    for (int k = 8; k >= 0; k--) {
#pragma HLS UNROLL
        ap_uint<18> t = ((ap_uint<18>)zcv) << k; if (v >= t) v = v - t; }
    return (zidx_t)v;
}
static zv_t rotz(zv_t c, zidx_t s, zidx_t zcv, zv_t mask) {         // result[z] = c[(z + s) mod Zc], 0 <= s < Zc
#pragma HLS INLINE
    ap_uint<10> l = (ap_uint<10>)zcv - s;
    return ((c >> s) | (c << l)) & mask;
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
    static zv_t cw[68];                        // codeword: one Zc-bit word per base-graph column
    static uint32_t done_count = 0;

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
    const zv_t mask = (~zv_t(0)) >> (ap_uint<9>)(384 - zc);
    const cnt_t A = (cnt_t)tb_bits, Kp = (cnt_t)kprime, K = (cnt_t)(22u * zc);
    const cnt_t dpc = (cnt_t)(cb_crc ? kprime - 24 : kprime);
    const small_t Qm = (small_t)qm, qm1 = (small_t)(qm - 1), ILS = (small_t)ils;
    // position whose successor is the first filler bit (fillers end at column 22, index 0)
    const col_t pre_fc = (col_t)(fill_idx == 0 ? fill_col - 1 : fill_col); const zidx_t pre_fi = (zidx_t)(fill_idx == 0 ? zc - 1 : fill_idx - 1);
    const cnt_t Ffill = (cnt_t)(22u * zc - kprime), fill_lo = (cnt_t)(kprime - 2u * zc), NmF = (cnt_t)(66u * zc - (22u * zc - kprime));
    const cnt_t tw1 = (cnt_t)(total_words - 1), tw = (cnt_t)total_words;
    uint64_t sh = x.data.to_uint64(); bool in_last = x.last;
    cnt_t src = 0, words_out = 0; crc_t crcA = 0; uint64_t ow = 0; small_t ob = 0; small_t inb = 0;
    const zidx_t a0 = modz((zidx_t)BG1_NZ_V[ILS][BG1_NZ_R0C22], Zc), a1 = modz((zidx_t)BG1_NZ_V[ILS][BG1_NZ_R1C22], Zc);

    CB: for (small_t r = 0; r < (small_t)num_cb; r++) {
        // ---- information bits, CRCs, fillers (one bit per clock, one column word written every Zc clocks)
        crc_t crcB = 0; col_t col = 0; zidx_t idx = 0; zv_t colw = 0;
        FILL: for (cnt_t k = 0; k < K; k++) {
#pragma HLS PIPELINE II=1
            bool bit = 0;
            if (k < dpc) {
                if (src < A) {
                    if (inb == 64) { if (!in_last) { axis64_t y = s_axis.read(); sh = y.data.to_uint64(); in_last = y.last; } else sh = 0; inb = 0; }
                    bit = sh & 1; sh >>= 1; inb++;
                    bool fb = ((crcA >> 23) & 1) ^ bit; crcA = (crc_t)((crcA << 1) & 0xFFFFFF); if (fb) crcA ^= (crc_t)0x864CFB;
                } else { bit = (crcA >> 23) & 1; crcA = (crc_t)((crcA << 1) & 0xFFFFFF); }
                src++;
                bool fb = ((crcB >> 23) & 1) ^ bit; crcB = (crc_t)((crcB << 1) & 0xFFFFFF); if (fb) crcB ^= (crc_t)0x800063;
            } else if (k < Kp) { bit = (crcB >> 23) & 1; crcB = (crc_t)((crcB << 1) & 0xFFFFFF); }
            colw[idx] = bit;
            if (idx == zm1) { cw[col] = colw; colw = 0; idx = 0; col++; } else idx++;
        }
        // ---- core parity: lambda_i = sum over information columns of rot(c_j, P_ij), rows 0..3
        zv_t lam[4]; zv_t acc = 0;
#pragma HLS ARRAY_PARTITION variable=lam complete
        CORE: for (nz_t nz = 0; nz < BG1_ROW_START[4]; nz++) {
#pragma HLS PIPELINE II=1
            const col_t c = BG1_NZ_COL[nz];
            zv_t v = acc;
            if (c < 22) v = acc ^ rotz(cw[c], modz((zidx_t)BG1_NZ_V[ILS][nz], Zc), Zc, mask);
            if (BG1_NZ_LAST[nz]) { lam[BG1_NZ_ROW[nz]] = v; acc = 0; } else acc = v;
        }
        {   const zv_t sum = lam[0] ^ lam[1] ^ lam[2] ^ lam[3];
            const zidx_t back = (a1 == 0) ? (zidx_t)0 : (zidx_t)(Zc - a1);
            const zv_t p1 = rotz(sum, back, Zc, mask);                 // rot(p1, a1) = sum
            const zv_t r0 = rotz(p1, a0, Zc, mask);
            const zv_t p2 = lam[0] ^ r0, p3 = lam[1] ^ sum ^ p2, p4 = lam[3] ^ r0;
            cw[22] = p1; cw[23] = p2; cw[24] = p3; cw[25] = p4; }
        // ---- extension parity rows actually needed by rate matching (they only read columns 0..25)
        const bool use_floor = r < (small_t)n_floor;
        const small_t nrows = (small_t)(use_floor ? rows_floor : rows_ceil);
        const cnt_t Eq = (cnt_t)(use_floor ? eq_floor : eq_ceil);
        const nz_t nz_end = BG1_ROW_START[nrows];
        acc = 0;
        EXT: for (nz_t nz = BG1_ROW_START[4]; nz < nz_end; nz++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=cw inter false
            const col_t c = BG1_NZ_COL[nz];
            zv_t v = acc;
            if (c < 26) v = acc ^ rotz(cw[c], modz((zidx_t)BG1_NZ_V[ILS][nz], Zc), Zc, mask);
            if (BG1_NZ_LAST[nz]) { cw[(col_t)(22 + BG1_NZ_ROW[nz])] = v; acc = 0; } else acc = v;
        }
        // ---- rate matching + bit interleaving: Qm read pointers; pointer i starts at e-index i*E/Qm
        col_t pc[8]; zidx_t pi[8];
#pragma HLS ARRAY_PARTITION variable=pc complete
#pragma HLS ARRAY_PARTITION variable=pi complete
        { cnt_t e = 0;
          PTR: for (small_t i = 0; i < Qm; i++) {
              cnt_t em = e;
              PTR_WRAP: while (em >= NmF) {
#pragma HLS PIPELINE II=1
                  em = em - NmF; }
              cnt_t k = (em >= fill_lo) ? (cnt_t)(em + Ffill) : em;
              col_t c = 2;
              PTR_DIV: while (k >= (cnt_t)Zc) {
#pragma HLS PIPELINE II=1
                  k = k - (cnt_t)Zc; c++; }
              pc[i & 7] = c; pi[i & 7] = (zidx_t)k;
              e = e + Eq;
          } }
        { const cnt_t E = (cnt_t)((unsigned)Eq * (unsigned)Qm);
          OUT: for (cnt_t n = 0; n < E; n++) {
#pragma HLS PIPELINE II=1
              col_t c = pc[0]; zidx_t ix = pi[0];
              const zv_t word = cw[c];
              bool bit = word[ix];
              { const bool wrap = (ix == zm1), pre = (c == pre_fc && ix == pre_fi), endc = (c == 67);
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
