// C testbench: argv[1] = transport blocks (1345 B each), argv[2] = reference Split-7.3 (2632 B each), [argv[3] = count]
#include "ldpc_tx_enc.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
static std::vector<uint8_t> slurp(const char* fn){ std::ifstream f(fn, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
int main(int argc, char** argv){
    const char* f1 = argc > 1 ? argv[1] : "tb_blocks.bin"; const char* f2 = argc > 2 ? argv[2] : "ref_split73.bin";
    auto tb = slurp(f1); auto ref = slurp(f2); const int TBB = 1345, REFB = 2632;
    size_t n = std::min(tb.size() / TBB, ref.size() / REFB); if (argc > 3) n = std::min<size_t>(n, atoi(argv[3]));
    if (!n) { printf("no test data\n"); return 1; }
    // N_RE=73 QPSK r=490/1024: A=10760 C=2 K'=5416 Zc=256 iLS=0 G=21054 -> E=10526,10528; rows 22; 329 data words + 3 flush
    hls::stream<axis64_t> in, out; uint32_t done = 0; size_t ok = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t buf[1352] = {0}; memcpy(buf, &tb[i * TBB], TBB);
        for (int wd = 0; wd < 169; wd++) { axis64_t x; uint64_t v; memcpy(&v, buf + 8 * wd, 8); x.data = v; x.keep = 0xFF; x.strb = 0xFF; x.last = (wd == 168); in.write(x); }
        ldpc_tx_enc(in, out, 1, 10760, 2, 1, 5416, 256, 0, 21, 40, 2, 1, 5263, 5264, 22, 22, 332, &done);
        bool same = out.size() == 332 && in.empty();
        for (int wd = 0; wd < 332 && same; wd++) { axis64_t y = out.read(); uint64_t e = 0; if (wd < 329) memcpy(&e, &ref[i * REFB + 8 * wd], 8); if (y.data.to_uint64() != e || y.last != (wd == 331)) { same = false; printf("packet %zu word %d mismatch\n", i, wd); } }
        while (!out.empty()) out.read();
        ok += same;
    }
    // bypass
    bool bp = true; for (int wd = 0; wd < 5; wd++) { axis64_t x; x.data = 0x1111111111111111ULL * (wd + 1); x.keep = 0xFF; x.strb = 0xFF; x.last = (wd == 4); in.write(x); }
    ldpc_tx_enc(in, out, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, &done);
    for (int wd = 0; wd < 5; wd++) { axis64_t y = out.read(); if (y.data.to_uint64() != 0x1111111111111111ULL * (wd + 1) || y.last != (wd == 4)) bp = false; }
    printf("HLS_TB encode exact=%zu of %zu, bypass=%s, packets_done=%u\n", ok, n, bp ? "ok" : "FAIL", done);
    return (ok == n && bp) ? 0 : 1;
}
