// Claude 2026-11-11: hot loop of the TAP <-> nl_eth_dma bridge (called from nl_tap_bridge.py through ctypes).
// Python still does all setup (overlay, NetworkLayer, buffer allocation, ring init, DMA start/stop); this function only
// moves frames.  Cache maintenance is done from user space with the arm64 DC CVAC / DC CIVAC instructions (allowed at EL0
// under Linux) instead of one XRT sync ioctl (~54 us) per buffer.  Build: gcc -O2 -shared -fPIC nl_bridge_core.c -o libnlbridge.so
#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <time.h>
#define D_CTRL 6
#define D_STS 7
#define STS_CMPLT 0x80000000u
#define STS_ERR 0x70000000u
#define CTRL_SOF (1u << 27)
#define CTRL_EOF (1u << 26)
#define MM2S_SR 0x04
#define MM2S_TAIL 0x10
#define S2MM_SR 0x34
#define S2MM_TAIL 0x40
#define SR_ERRMASK 0x770
static inline uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static inline void dc_clean(const void* p, size_t n) {            // write dirty lines to memory (before the DMA reads them)
    uintptr_t a = (uintptr_t)p & ~63ul, e = (uintptr_t)p + n;
    for (; a < e; a += 64) __asm__ volatile("dc cvac, %0" : : "r"(a));
    __asm__ volatile("dsb sy");
}
static inline void dc_drop(const void* p, size_t n) {             // clean+invalidate (before the CPU reads what the DMA wrote)
    uintptr_t a = (uintptr_t)p & ~63ul, e = (uintptr_t)p + n;
    for (; a < e; a += 64) __asm__ volatile("dc civac, %0" : : "r"(a));
    __asm__ volatile("dsb sy");
}
struct nlb_state {
    volatile uint32_t* regs;      // nl_eth_dma register block
    uint32_t* txd; uint32_t* rxd; // descriptor rings (ndesc x 16 words, 64-byte descriptors)
    uint8_t** txb; uint8_t** rxb; // per-descriptor buffers
    uint32_t txd_phys, rxd_phys;  // physical address of descriptor 0 of each ring
    uint32_t ndesc, bufsz;
    uint32_t tx_idx, rx_idx;      // next descriptor to use / to look at
    uint8_t tx_busy[256];
    int tap_fd; int busy_poll;
    uint64_t n_tx, n_rx, drops, tx_bytes, rx_bytes;
    uint32_t err_sr;              // non-zero: a DMA error status was seen (loop returned -1)
};
// Runs for at most max_ns nanoseconds, then returns 0 (so Python can look at signals / the time limit); -1 on DMA error.
int nlb_run(struct nlb_state* s, uint64_t max_ns) {
    const uint64_t end = now_ns() + max_ns; unsigned spin = 0;
    for (;;) {
        int did = 0;
        for (;;) {                                               // FPGA -> Linux
            uint32_t* d = s->rxd + 16 * s->rx_idx;
            dc_drop(d, 64);
            uint32_t sts = d[D_STS];
            if (!(sts & STS_CMPLT)) break;
            if (sts & STS_ERR) { s->err_sr = sts; return -1; }
            uint32_t n = sts & 0xFFFF;
            dc_drop(s->rxb[s->rx_idx], n);
            if (write(s->tap_fd, s->rxb[s->rx_idx], n) < 0) s->drops++;
            d[D_STS] = 0; dc_clean(d, 64);
            s->regs[S2MM_TAIL / 4] = s->rxd_phys + 64 * s->rx_idx;
            s->regs[(S2MM_TAIL + 4) / 4] = 0;
            s->rx_idx = (s->rx_idx + 1) % s->ndesc; s->n_rx++; s->rx_bytes += n; did = 1;
        }
        for (int burst = 0; burst < 16; burst++) {               // Linux -> FPGA
            uint32_t i = s->tx_idx; uint32_t* d = s->txd + 16 * i;
            if (s->tx_busy[i]) { dc_drop(d, 64); if (!(d[D_STS] & STS_CMPLT)) break; }   // ring full: leave the frame in the TAP queue
            ssize_t n = read(s->tap_fd, s->txb[i], s->bufsz);
            if (n <= 0) break;
            dc_clean(s->txb[i], (size_t)n);
            d[D_STS] = 0; d[D_CTRL] = (uint32_t)n | CTRL_SOF | CTRL_EOF; dc_clean(d, 64);
            s->regs[MM2S_TAIL / 4] = s->txd_phys + 64 * i;
            s->regs[(MM2S_TAIL + 4) / 4] = 0;
            s->tx_busy[i] = 1; s->tx_idx = (i + 1) % s->ndesc; s->n_tx++; s->tx_bytes += (uint64_t)n; did = 1;
        }
        if (!did) {
            if ((++spin & 0xFF) == 0) {
                uint32_t a = s->regs[MM2S_SR / 4], b = s->regs[S2MM_SR / 4];
                if ((a | b) & SR_ERRMASK) { s->err_sr = (a & SR_ERRMASK) ? a : b; return -1; }
                if (now_ns() > end) return 0;
            }
            if (!s->busy_poll) { struct pollfd p = { s->tap_fd, POLLIN, 0 }; poll(&p, 1, 1); }
        } else if (now_ns() > end) return 0;
    }
}
