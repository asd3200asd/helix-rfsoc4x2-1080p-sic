// Claude 2026-11-11: multi-configuration testbench for ldpc_tx_enc.  argv[1] = cases.bin (gen_ref_cases.py).
// Parameters come from the same split6::derive() the host uses.  Each case is run twice back to back.
#include "ldpc_tx_enc.h"
#include "split6_params.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
int main(int argc, char** argv){
    std::ifstream f(argc > 1 ? argv[1] : "cases.bin", std::ios::binary); std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.empty()) { printf("no cases\n"); return 1; }
    hls::stream<axis64_t> in, out; uint32_t done = 0; size_t pos = 0, ncase = 0, ok = 0;
    while (pos + 12 <= d.size()) {
        uint32_t A, G, Qm; memcpy(&A, &d[pos], 4); memcpy(&G, &d[pos + 4], 4); memcpy(&Qm, &d[pos + 8], 4); pos += 12;
        const uint8_t* tb = &d[pos]; pos += A / 8; const uint8_t* ref = &d[pos]; pos += (G + 7) / 8; ncase++;
        split6::enc_params p; if (!split6::derive(A, G, Qm, 3, p)) { printf("case A=%u G=%u Qm=%u: derive() rejected\n", A, G, Qm); continue; }
        bool same = true;
        for (int rep = 0; rep < 2 && same; rep++) {
            size_t nw = (A / 8 + 7) / 8; std::vector<uint8_t> buf(nw * 8, 0); memcpy(buf.data(), tb, A / 8);
            for (size_t w = 0; w < nw; w++) { axis64_t x; uint64_t v; memcpy(&v, &buf[8 * w], 8); x.data = v; x.keep = 0xFF; x.strb = 0xFF; x.last = (w == nw - 1); in.write(x); }
            ldpc_tx_enc(in, out, 1, p.A, p.C, p.cb_crc, p.Kp, p.Zc, p.iLS, p.fill_col, p.fill_idx, p.Qm, p.n_floor, p.eq_floor, p.eq_ceil, p.rows_floor, p.rows_ceil, p.total_words, &done);
            size_t dw = (G + 63) / 64; std::vector<uint8_t> r(dw * 8, 0); memcpy(r.data(), ref, (G + 7) / 8);
            if (out.size() != p.total_words || !in.empty()) { same = false; printf("  word count %zu != %u\n", (size_t)out.size(), p.total_words); }
            for (size_t w = 0; w < p.total_words && same; w++) { axis64_t y = out.read(); uint64_t e = 0; if (w < dw) memcpy(&e, &r[8 * w], 8); if (y.data.to_uint64() != e || y.last != (w == p.total_words - 1)) { same = false; printf("  mismatch at word %zu\n", w); } }
            while (!out.empty()) out.read(); while (!in.empty()) in.read();
        }
        printf("case A=%-6u G=%-6u Qm=%u C=%u Zc=%-3u rows=%u/%u : %s\n", A, G, Qm, p.C, p.Zc, p.rows_floor, p.rows_ceil, same ? "exact" : "FAIL"); ok += same;
    }
    printf("CASES exact=%zu of %zu\n", ok, ncase); return ok == ncase ? 0 : 1;
}
