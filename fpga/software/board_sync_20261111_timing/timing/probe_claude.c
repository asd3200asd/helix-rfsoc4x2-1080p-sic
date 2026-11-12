// Claude 2026-11-11: hardware event probe for helix_nlsg_s6.bit (run as root, busy-polls /dev/mem on CPU 3).
// Logs with CLOCK_MONOTONIC ns:  E <packets_done>          ldpc_tx_enc finished a transport block (reg 0xB0011090)
//                                T <frame bytes>           nl_eth_dma MM2S started a frame towards the FPGA (CURDESC moved)
//                                R <frame bytes>           nl_eth_dma S2MM completed a frame from the FPGA (CURDESC moved)
// usage: sudo ./probe_claude <seconds> <outfile>
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>
#include <sys/mman.h>
#define MAXEV 4000000
static inline uint64_t now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
struct ev { uint64_t t; uint32_t v; char k; };
static volatile uint32_t* map(int fd, off_t a, size_t n) { void* p = mmap(0, n, PROT_READ, MAP_SHARED, fd, a); return p == MAP_FAILED ? 0 : (volatile uint32_t*)p; }
int main(int argc, char** argv) {
    double secs = argc > 1 ? atof(argv[1]) : 5; const char* out = argc > 2 ? argv[2] : "probe.log";
    cpu_set_t m; CPU_ZERO(&m); CPU_SET(3, &m); sched_setaffinity(0, sizeof m, &m);
    int fd = open("/dev/mem", O_RDONLY | O_SYNC); if (fd < 0) { perror("/dev/mem"); return 1; }
    volatile uint32_t* enc = map(fd, 0xB0011000, 0x1000); volatile uint32_t* dma = map(fd, 0xB0100000, 0x1000);
    if (!enc || !dma) { perror("mmap"); return 1; }
    struct ev* e = malloc(sizeof(struct ev) * MAXEV); size_t n = 0;
    uint32_t pd = enc[0x90 / 4], mm = dma[0x08 / 4], s2 = dma[0x38 / 4];
    off_t tbase = mm & ~0xFFFul, rbase = s2 & ~0xFFFul;
    volatile uint32_t* tring = mm ? map(fd, tbase, 0x2000) : 0; volatile uint32_t* rring = s2 ? map(fd, rbase, 0x2000) : 0;
    uint64_t end = now() + (uint64_t)(secs * 1e9), loops = 0, t0 = now();
    while (n < MAXEV - 4) {
        uint32_t a = enc[0x90 / 4], b = dma[0x08 / 4], c = dma[0x38 / 4]; uint64_t t = now(); loops++;
        if (a != pd) { pd = a; e[n].t = t; e[n].k = 'E'; e[n].v = a; n++; }
        if (b != mm) { mm = b; uint32_t len = 0xFFFFFFFF; if (tring && (off_t)b >= tbase && (off_t)b < tbase + 0x2000 - 64) len = tring[(b - tbase) / 4 + 6] & 0xFFFF; e[n].t = t; e[n].k = 'T'; e[n].v = len; n++; }
        if (c != s2) { uint32_t len = 0xFFFFFFFF; if (rring && (off_t)s2 >= rbase && (off_t)s2 < rbase + 0x2000 - 64) { for (int k = 0; k < 50; k++) { uint32_t st = rring[(s2 - rbase) / 4 + 7]; if (st & 0x80000000u) { len = st & 0xFFFF; break; } } } s2 = c; e[n].t = t; e[n].k = 'R'; e[n].v = len; n++; }
        if (t > end) break;
    }
    uint64_t t1 = now(); FILE* f = fopen(out, "w"); if (!f) { perror(out); return 1; }
    fprintf(f, "# loops %llu avg_loop_ns %.1f events %zu\n", (unsigned long long)loops, (double)(t1 - t0) / loops, n);
    for (size_t i = 0; i < n; i++) fprintf(f, "%c %u %llu\n", e[i].k, e[i].v, (unsigned long long)e[i].t);
    fclose(f); printf("PROBE_DONE events=%zu avg_loop_ns=%.1f\n", n, (double)(t1 - t0) / loops); return 0;
}
