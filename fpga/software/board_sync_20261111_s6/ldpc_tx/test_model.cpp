#include "ldpc_tx_model.h"
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>
#include <chrono>
static std::vector<uint8_t> slurp(const char* fn){ std::ifstream f(fn, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
int main(int argc, char** argv){
    auto tb = slurp(argv[1]); auto ref = slurp(argv[2]); const int TBB = 1345, REFB = 2632, G = 21054;
    ldpctx::Params p; if (!ldpctx::derive(TBB * 8, G, 2, p)) { printf("derive failed\n"); return 2; }
    printf("params A=%d C=%d Kp=%d K=%d Zc=%d iLS=%d G=%d\n", p.A, p.C, p.Kp, p.K, p.Zc, p.iLS, p.G);
    size_t n = std::min(tb.size() / TBB, ref.size() / REFB), ok = 0; static uint8_t bits[ldpctx::MAXG];
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < n; i++) {
        if (!ldpctx::encode(&tb[i * TBB], p, bits)) { printf("encode failed\n"); return 2; }
        std::vector<uint8_t> packed(REFB, 0); for (int b = 0; b < G; b++) packed[b >> 3] |= bits[b] << (b & 7);
        bool same = true; for (int b = 0; b < REFB; b++) if (packed[b] != ref[i * REFB + b]) { same = false; if (ok == i) printf("first mismatch packet %zu byte %d\n", i, b); break; }
        ok += same;
    }
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("MODEL_CHECK exact=%zu of %zu  (%.2f ms per TB on this CPU)\n", ok, n, 1000 * dt / n);
    return ok == n ? 0 : 1;
}
