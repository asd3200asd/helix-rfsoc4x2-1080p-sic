// Claude 2026-11-13 fdx_bidi: fdx_sic_claude for TWO full-duplex boards (both transmit and receive on the same frequency).
// HELIX_NODE=0|1 (required): every transmitted TB carries the node id in bit 15 of its length field; the receiver drops TBs
// carrying its own id (rx_self) so its own transmission can never be mistaken for the remote stream (both start at seq 0).
// Copies per TB adapt to the input rate (ONEWAY_REPEAT is the maximum, minimum 2).
// The receiver re-arms a partly received batch after ONEWAY_REARM_S (0.5 s) without packets (lost datagrams would otherwise stall it).
// Everything else (pacing, triggers, reorder) is unchanged from fdx_sic_claude.
// Claude 2026-11-12 fdx_sic: oneway2 with (1) ONEWAY_PKTLOG records also carrying the TB CRC (16 bytes: u64 t_ns, u8 good, u8 0, u16 seq, u32 crc)
// so packets can be attributed to the local or the remote stream offline, (2) the confirmed-trigger TX path in every mode.
// Used for the SIC remote-preservation test: ONEWAY_MODE=both on the full-duplex board while another board transmits.
// 2026-11-12: rx-only watchdog (ONEWAY_RX_WD_S seconds, default 0 = off; only safe while the transmitter is silent) re-initialises the receiver when packets stop; len==0 packets are ignored.
// Claude 2026-11-11 oneway2: same as oneway_split6_claude, but the receiver REORDERS: a TB whose first copy was bad is taken from
// its repeat (which arrives up to one burst later) instead of being dropped as a "duplicate".  A hole is declared only after
// ONEWAY_REORDER (default max_burst) newer TBs or 150 ms without progress.  TX side is unchanged (either binary can transmit).
// Claude 2026-11-11: ONE-WAY link for two boards (one transmits, one receives, no feedback channel), TX/RX split 6,
// helix_nlsg_s6v2.bit, N_RE=73 QPSK r=490/1024.  Same TB format as stream_relay_split6_claude (u16 len|u16 seq|u32 crc|payload).
// usage: ONEWAY_MODE=tx|rx|both [ONEWAY_REPEAT=2] [HELIX_FREQ_MHZ=1720] oneway_split6_claude [tx_ifg=180000] [unused] [max_burst=256] [record]
//   tx   : stdin -> air.  No acknowledgements exist, so every TB is sent ONEWAY_REPEAT times (the copies are half a burst apart)
//          and the feed is paced by the clock (one TB per tx_ifg) instead of by received packets.
//   rx   : air -> stdout.  Arms the receiver for ONEWAY_RXN (4096) packets at a time, keeps CRC-good TBs, drops duplicates,
//          and after a hole resumes at the next H.264 SPS (ONEWAY_RAW=1: write everything that is good, for file tests).
//   both : tx and rx threads on ONE board without any coupling between them - the single-board rehearsal of the two-board setup.
// Notes: the RX Stream Manager stays in SEND until its packet count is reached, so the receiver must always be restarted with
// the same ONEWAY_RXN (a new trigger then simply continues the old count) - or reload the bit.  rx mode ends on SIGINT/SIGTERM
// or after ONEWAY_RX_IDLE_S seconds without packets (0 = never).  Two boards have independent oscillators: carrier/sample
// offset between boards has NOT been tested yet (single board only).
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include "helix_split6.h"
#include <iostream>
#include <vector>
#include <deque>
#include <functional>
#include <csignal>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <unistd.h>
const char* fpga_ip = "192.168.10.2";
static uint64_t now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static uint32_t crc_tab[256];
static void crc_init() { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_tab[i] = c; } }
static uint32_t crc32(const uint8_t* p, size_t n) { uint32_t c = 0xFFFFFFFFu; while (n--) c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8); return ~c; }
static bool trig(helix::helix& r, const char* name, std::vector<std::string> args) { helix::cmd_struct c; c.cmd = name; c.cmdArgs = args; return r.cmd->sendCmd(c, true) == UDP_CMD_ACK; }
static const size_t TBB = 1345, TXB = 1352, RXB = 1408, HDR = 8, PAY = TBB - HDR, IN_MAX = 16u << 20, OUT_MAX = 6u << 20;
static std::mutex in_m; static std::vector<uint8_t> in_buf; static std::atomic<bool> in_eof{false};
static std::mutex out_m; static std::condition_variable out_cv; static std::deque<std::vector<uint8_t>> out_q; static size_t out_bytes = 0; static bool out_done = false;
static std::atomic<uint64_t> out_drops{0};
static std::atomic<bool> g_stop{false};
static std::mutex cmd_m;
int main(int argc, char** argv){
    const char* nodes = getenv("HELIX_NODE"); if (!nodes || (strcmp(nodes, "0") && strcmp(nodes, "1"))) { std::cerr << "FATAL HELIX_NODE must be 0 or 1\n"; return 2; }
    const unsigned node = nodes[0] - '0';
    const std::string mode = getenv("ONEWAY_MODE") ? getenv("ONEWAY_MODE") : "both"; const bool do_tx = mode != "rx", do_rx = mode != "tx";
    signal(SIGINT, [](int){ g_stop = true; }); signal(SIGTERM, [](int){ g_stop = true; });
    const uint32_t ifg = argc > 1 ? std::stoul(argv[1]) : 180000; const size_t window = argc > 2 ? std::stoul(argv[2]) : 32;
    const size_t MAXB = argc > 3 ? std::stoul(argv[3]) : 256; FILE* rec = argc > 4 ? fopen(argv[4], "wb") : nullptr;
    const int outfd = dup(1); dup2(2, 1);                     // library prints go to stderr; stdout carries only the stream
    crc_init();
    auto radio = helix::helix(fpga_ip);
    std::vector<uint8_t> pkt(TXB, 0);
    // Full (re)initialisation.  Also used as the recovery path after a control time-out or an incomplete burst: RX is
    // switched off, late datagrams are drained, and the whole radio/encoder configuration is written again.
    auto setup = [&](bool first) -> bool {
        if (!first) { radio.control->enable_rx_radio(false); usleep(300000);
            std::vector<uint8_t> d(RXB); for (int i = 0; i < 2000 && radio.udp->data_socket.recv(d.data(), RXB) > 0; i++) {} }
        helix::stream_str sc{}; sc.udp_rx_mss = RXB; sc.udp_rx_ifg = 128; sc.radio_tx_mss = TXB; sc.radio_tx_ifg = ifg;
        radio.control->set_streaming_param(sc);
        radio.control->configure_radio(SPLIT_6, SPLIT_6, BW_MODE_HIGH, 73, MOD_QPSK, 490.0/1024, 0);
        auto rp = radio.control->get_radio_config();
        // 2026-11-12: the original HELIX two-node examples (throughput_experiment_tx/rx) load the SSB on the transmitter only.
        if (do_tx || !getenv("ONEWAY_NO_SSB")) radio.control->load_SSB(load_waveform_from_file(getenv("HELIX_SSB_FILE") ? getenv("HELIX_SSB_FILE") : "matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));   /* HELIX_SSB_FILE: e.g. an SSB without PSS so this board's receiver does not synchronise to its own transmitter */
        { const float fmhz = getenv("HELIX_FREQ_MHZ") ? atof(getenv("HELIX_FREQ_MHZ")) : 400;   // centre frequency (antenna: 1740/1840)
          const float ftx = getenv("HELIX_TX_FREQ_MHZ") ? atof(getenv("HELIX_TX_FREQ_MHZ")) : fmhz;   /* diagnostic: move the local TX out of band */
          radio.control->set_freq_band({{ftx,RFDC_DAC_TYPE,0,0,true},{-fmhz,RFDC_ADC_TYPE,2,0,true}}); }
        if (!split6::enable(radio, rp->tbs, rp->num_sch * MOD_QPSK, MOD_QPSK)) return false;
        usleep(10000); memset(pkt.data(), 0, TXB);
        if (do_tx) { if (!trig(radio, "triggerTX ", {std::to_string(TXB)})) return false;
            radio.udp->data_socket.send(pkt.data(), TXB); usleep(20000); }   // warm-up slot
        if (do_rx) radio.control->enable_rx_radio(true);
        return true; };
    if (!setup(true)) return 2;
    std::cerr << "ONEWAY_READY mode=" << mode << "  ifg=" << ifg << " window=" << window << " max_burst=" << MAXB << std::endl;
    std::thread reader([&] { std::vector<uint8_t> b(65536);
        for (;;) { ssize_t n = read(0, b.data(), b.size()); if (n <= 0) break;
            for (;;) { { std::lock_guard<std::mutex> l(in_m); if (in_buf.size() < IN_MAX) { in_buf.insert(in_buf.end(), b.begin(), b.begin() + n); break; } } usleep(1000); } }
        in_eof = true; });
    std::thread writer([&] { for (;;) { std::vector<uint8_t> v; { std::unique_lock<std::mutex> l(out_m); out_cv.wait(l, [] { return !out_q.empty() || out_done; });
                if (out_q.empty()) break; v = std::move(out_q.front()); out_q.pop_front(); out_bytes -= v.size(); }
            size_t o = 0; while (o < v.size()) { ssize_t n = write(outfd, v.data() + o, v.size() - o); if (n <= 0) return; o += n; } } });
    bool dropping = false; std::vector<uint8_t> carry;
    auto emit = [&](const uint8_t* p, size_t n) {
        if (rec) fwrite(p, 1, n, rec);
        std::unique_lock<std::mutex> l(out_m);
        if (!dropping && out_bytes > OUT_MAX) { out_q.clear(); out_bytes = 0; dropping = true; carry.clear(); out_drops++; }
        if (dropping) { size_t c0 = carry.size(); carry.insert(carry.end(), p, p + n); size_t at = (size_t)-1;
            for (size_t i = 0; i + 4 < carry.size(); i++) if (!carry[i] && !carry[i+1] && !carry[i+2] && carry[i+3] == 1 && (carry[i+4] & 0x1F) == 7) { at = i; break; }
            (void)c0; if (at == (size_t)-1) { if (carry.size() > 4) carry.erase(carry.begin(), carry.end() - 4); return; }
            std::vector<uint8_t> v(carry.begin() + at, carry.end()); carry.clear(); dropping = false; out_bytes += v.size(); out_q.push_back(std::move(v)); }
        else { out_q.emplace_back(p, p + n); out_bytes += n; }
        l.unlock(); out_cv.notify_one(); };
    const size_t R = getenv("ONEWAY_REPEAT") ? std::stoul(getenv("ONEWAY_REPEAT")) : 2, RXN = getenv("ONEWAY_RXN") ? std::stoul(getenv("ONEWAY_RXN")) : 4096;
    const bool raw = getenv("ONEWAY_RAW") != nullptr; const double idle_s = getenv("ONEWAY_RX_IDLE_S") ? atof(getenv("ONEWAY_RX_IDLE_S")) : 0;
    const uint64_t period = (uint64_t)ifg + 3000;                 // ns per TB on air (measured: tx_ifg + ~2 us)
    auto ctrig = [&](const char* n, std::vector<std::string> a) { std::lock_guard<std::mutex> l(cmd_m); return trig(radio, n, a); };
    std::atomic<uint64_t> tx_trig_to{0}, tx_dropped_bursts{0}, tx_unique{0}, tx_sent{0}, tx_bursts{0}, rx_pk{0}, rx_good{0}, rx_bad{0}, rx_dup{0}, rx_holes{0}, rx_bytes{0}, rx_self{0}, rx_rearm{0}, tx_fill{0}; std::atomic<uint64_t> r_sum{0}, r_n{0}; std::atomic<bool> tx_done{!do_tx};
    std::vector<uint8_t> dummy(TXB, 0); dummy[0] = 0xFF; dummy[1] = 0xFF;
    auto paced = [&](size_t B, const std::function<const uint8_t*(size_t)>& get) {     // one TX burst, clock-paced
        static const int test_n = getenv("ONEWAY_TEST_TRIGFAIL") ? atoi(getenv("ONEWAY_TEST_TRIGFAIL")) : 0; static int tcount = 0;   // test hook: pretend every Nth trigger timed out
        if ((test_n && ++tcount % test_n == 0) || !ctrig("triggerTX ", {std::to_string(B * TXB)})) {
            // 2026-11-12: an unconfirmed trigger must NOT be followed by data.  Data and control share the FPGA network input, so
            // untriggered data fills the TX FIFO, the back-pressure then blocks every later control packet, and the transmitter is
            // dead until the bit is reloaded.  Tx-only mode: reset the TX path (full re-init) and trigger again; if that also
            // fails, drop this burst.
            tx_trig_to++; std::cerr << "ONEWAY_WARN TX trigger timeout (" << tx_trig_to.load() << ")" << (do_rx ? "" : ": waiting for a late ACK / retrying") << std::endl;
            { /* fdx: always wait for or retry the trigger, never send untriggered data */ bool ok = false;
                // 2026-11-13 bidi: the control lock is taken per attempt (not for all retries, up to ~5 s), so the receiver can
                // still re-trigger its next batch in between; otherwise a TX trigger time-out also stalls reception.
                for (int attempt = 0; attempt < 4 && !ok; attempt++) {
                    if (attempt) usleep(20000);
                    std::lock_guard<std::mutex> l(cmd_m);
                    uint32_t late = 0; bool got = false;                                   // a late ACK means the trigger WAS executed
                    for (int i = 0; i < 3 && !got; i++) got = radio.udp->control_socket.recv(&late, sizeof late) > 0;   // 3 x 200 ms
                    if (got && late == UDP_CMD_ACK && !(test_n && attempt == 0)) { ok = true; break; }
                    ok = trig(radio, "triggerTX ", {std::to_string(B * TXB)}); }
                if (!ok) { tx_dropped_bursts++; std::cerr << "ONEWAY_WARN trigger not confirmed after retries: burst dropped" << std::endl; return; } } }
        const uint64_t t0 = now(); std::vector<uint8_t> p(TXB, 0);
        for (size_t k = 0; k < B; k++) { while (k >= 24 + (now() - t0) / period) usleep(40);
            memcpy(p.data(), get(k), TBB); radio.udp->data_socket.send(p.data(), TXB); }
        while (now() < t0 + (B + 2) * period + 1500000) usleep(100); };
    const uint64_t T0 = now();
    // 2026-11-13 bidi: ONEWAY_SPREAD=1 (default) spreads the R copies of a TB over R consecutive bursts (burst k carries the new
    // TBs of k, k-1, ..., k-R+1) instead of repeating them inside one burst, so one burst lost while both boards transmit does not
    // lose those TBs.  When the input pauses, the remaining copies are flushed with bursts that carry no new TBs.
    const bool spread = !getenv("ONEWAY_SPREAD") || atoi(getenv("ONEWAY_SPREAD")) != 0;
    // ONEWAY_PREFILL=1: until the first input byte arrives, transmit bursts of 32 filler TBs (invalid length, every receiver counts
    // them as bad and drops them) so the SIC can be calibrated on this board's own signal before any real data is sent.
    const bool prefill = getenv("ONEWAY_PREFILL") && atoi(getenv("ONEWAY_PREFILL")) != 0;
    std::thread txt([&] { if (!do_tx) return; uint16_t seq = 0; std::vector<uint8_t> txs; const size_t NU = std::max<size_t>(1, MAXB / R);
        std::deque<std::vector<uint8_t>> hist; std::vector<uint8_t> burst; uint64_t t_burst = now(); bool got_data = false; size_t r_eff = R; uint64_t t_radj = 0;
        auto send_hist = [&](std::vector<uint8_t>&& fresh) {      // fresh = new TBs (possibly none); send them with the older copies
            // 2026-11-13 bidi: adaptive repetition.  The camera's own H.264 rate varies 2..6 Mbit/s with the scene; 6 copies of 6 Mbit/s
            // would need ~85 % of the air time and the transmitter falls behind (trigger time-outs, dropped bursts).  When input
            // piles up the number of copies goes down (not below 2) and slowly back up when the backlog is gone.
            { size_t backlog; { std::lock_guard<std::mutex> l(in_m); backlog = in_buf.size(); }
              if (backlog > 96 * PAY && r_eff > 2 && now() - t_radj > 200000000ull) { r_eff--; t_radj = now(); }
              else if (backlog < 16 * PAY && r_eff < R && now() - t_radj > 2000000000ull) { r_eff++; t_radj = now(); }
              r_sum += r_eff; r_n++; }
            hist.push_front(std::move(fresh)); while (hist.size() > r_eff) hist.pop_back();
            burst.clear(); for (auto& h : hist) burst.insert(burst.end(), h.begin(), h.end());
            const size_t B = burst.size() / TBB; if (B) { paced(B, [&](size_t k) { return burst.data() + k * TBB; }); tx_sent += B; tx_bursts++; }
            bool any = false; for (auto& h : hist) any |= !h.empty(); if (!any) hist.clear(); t_burst = now(); };
        for (;;) { size_t avail; { std::lock_guard<std::mutex> l(in_m); avail = in_buf.size(); }
            if (avail) got_data = true;
            if (!avail && prefill && !got_data && !g_stop) { paced(32, [&](size_t) { return dummy.data(); }); tx_fill++; usleep(20000); continue; }
            if (!avail) { if (spread && !hist.empty() && now() - t_burst > 20000000ull) { send_hist({}); continue; }
                if (in_eof || g_stop) break; usleep(500); continue; }
            if (avail < 32 * PAY && !in_eof) usleep(3000);
            size_t n = 0; { std::lock_guard<std::mutex> l(in_m); size_t off = 0; txs.assign(NU * TBB, 0);
                while (n < NU && off < in_buf.size()) { size_t len = std::min(PAY, in_buf.size() - off); uint8_t* t = txs.data() + n * TBB;
                    memcpy(t + HDR, in_buf.data() + off, len); uint32_t c = crc32(t + HDR, len); uint16_t sq = seq++;
                    t[0] = len & 0xFF; t[1] = (len >> 8) | (node << 7); t[2] = sq & 0xFF; t[3] = sq >> 8; memcpy(t + 4, &c, 4); off += len; n++; }
                in_buf.erase(in_buf.begin(), in_buf.begin() + off); }
            if (spread) { txs.resize(n * TBB); send_hist(std::move(txs)); tx_unique += n; continue; }
            paced(n * R, [&](size_t k) { return txs.data() + (k % n) * TBB; }); tx_unique += n; tx_sent += n * R; tx_bursts++; }
        tx_done = true; });
    std::atomic<bool> rx_done_flag{!do_rx};
    std::thread rxthr([&] { struct Fin { std::atomic<bool>& f; ~Fin() { f = true; } } fin{rx_done_flag}; if (!do_rx) return; std::vector<uint8_t> rx(RXB), tbv(TBB); bool have = false; uint16_t next = 0; uint64_t t_last = now(), t_stall = now();
        const uint16_t W = getenv("ONEWAY_REORDER") ? (uint16_t)atoi(getenv("ONEWAY_REORDER")) : (uint16_t)MAXB; std::map<uint16_t, std::vector<uint8_t>> pend;
        const double rearm_s = getenv("ONEWAY_REARM_S") ? atof(getenv("ONEWAY_REARM_S")) : 0.5; bool reinit = false; const double wd_s = getenv("ONEWAY_RX_WD_S") ? atof(getenv("ONEWAY_RX_WD_S")) : 0.0;
        FILE* pktlog = getenv("ONEWAY_PKTLOG") ? fopen(getenv("ONEWAY_PKTLOG"), "wb") : nullptr;
        auto mark_drop = [&] { if (!raw) { std::lock_guard<std::mutex> l(out_m); dropping = true; carry.clear(); } };
        auto drain = [&](bool force) { for (;;) {
                for (auto it = pend.find(next); it != pend.end(); it = pend.find(next)) { rx_good++; rx_bytes += it->second.size(); emit(it->second.data(), it->second.size()); pend.erase(it); next++; t_stall = now(); }
                if (pend.empty()) return;
                uint16_t dmin = 65535, dmax = 0; for (auto& kv : pend) { uint16_t dd = kv.first - next; dmin = std::min(dmin, dd); dmax = std::max(dmax, dd); }
                if (!force && dmax < W && now() - t_stall < 150000000ull) return;
                rx_holes += dmin; mark_drop(); next += dmin; } };
        for (bool run = true; run;) {
            if (!ctrig("triggerRX ", {std::to_string(RXN * RXB), "0", "0", "0", std::to_string(RXN)})) std::cerr << "RX trigger timeout\n";
            for (size_t k = 0; k < RXN;) { if (g_stop && !do_tx) { run = false; break; }   // 2026-11-12: also stop while packets keep arriving
                ssize_t nb = radio.udp->data_socket.recv(rx.data(), RXB);
                if (nb != (ssize_t)RXB) { const double idle = (now() - t_last) / 1e9;
                    if (g_stop || (idle_s > 0 && idle > idle_s) || (do_tx && tx_done && idle > 0.6)) {
                        if (do_tx && !g_stop) { size_t rem = RXN - k;                  // single board: complete the armed count so the SM returns to idle
                            for (int a = 0; a < 6 && rem; a++) { std::thread f([&] { paced(rem, [&](size_t) { return dummy.data(); }); });
                                size_t got = 0; for (int miss = 0; got < rem && miss < 3;) { if (radio.udp->data_socket.recv(rx.data(), RXB) == (ssize_t)RXB) got++; else miss++; }
                                f.join(); rem -= got; }
                            if (rem) std::cerr << "ONEWAY_WARN receiver left armed (" << rem << " packets short); reload the bit before the next run\n"; }
                        run = false; break; }
                    drain(false);
                    // 2026-11-13 bidi: the host can lose datagrams, so the Stream Manager may finish its count of RXN while k < RXN
                    // and the receiver would then wait forever.  After rearm_s without packets in a partly received batch, trigger
                    // the next batch (if the SM is still counting, a new trigger just continues the old count).
                    if (k > 0 && idle > rearm_s) { std::cerr << "ONEWAY_RX_REARM partial_batch=" << k << "/" << RXN << std::endl; rx_rearm++; break; }
                    if (!do_tx && wd_s > 0 && rx_pk > 0 && !reinit && idle > wd_s) {   // receive-only watchdog: the FPGA receive path sometimes stops delivering mid-batch
                        reinit = true; drain(true); have = false; mark_drop();   /* a restarted sender begins at seq 0 again */ std::cerr << "ONEWAY_WARN no packets for " << idle << " s: re-initialising the receiver" << std::endl; setup(false); break; }
                    continue; }
                k++; rx_pk++; t_last = now(); reinit = false; if (pktlog) { struct { uint64_t t; uint8_t ok; uint8_t z; uint16_t sq; uint32_t c; } __attribute__((packed)) e{t_last, 0, 0, 0, 0}; size_t l0 = (rx[0] | (rx[1] << 8)) & 0x7FFF; std::vector<uint8_t> tb0(TBB); memcpy(tb0.data(), rx.data(), 674); memcpy(tb0.data() + 674, rx.data() + 704, 671); uint32_t c0; memcpy(&c0, tb0.data() + 4, 4); e.ok = (l0 && l0 <= PAY && crc32(tb0.data() + HDR, l0) == c0); e.z = rx[1] >> 7;   /* z = sender node id */ e.sq = tb0[2] | (tb0[3] << 8); e.c = c0; fwrite(&e, 1, sizeof e, pktlog); } memcpy(tbv.data(), rx.data(), 674); memcpy(tbv.data() + 674, rx.data() + 704, 671);
                const uint8_t* t = tbv.data(); size_t len = (t[0] | (t[1] << 8)) & 0x7FFF; const unsigned from = t[1] >> 7; uint16_t sq = t[2] | (t[3] << 8); uint32_t c; memcpy(&c, t + 4, 4);
                if (len == 0 || len > PAY || crc32(t + HDR, len) != c) { rx_bad++;
                    continue; }
                if (from == node) { rx_self++; continue; }
                if (!have) { have = true; next = (sq < W) ? 0 : sq; t_stall = now(); }
                { int16_t d = (int16_t)(sq - next);
                  if (d < 0 && d > -20000) { rx_dup++; continue; }
                  if (d <= -20000 || d > 8192) { drain(true); rx_holes++; mark_drop(); next = sq; }      // sender restarted
                  if (pend.count(sq)) { rx_dup++; continue; }
                  if (pend.empty()) t_stall = now();
                  pend.emplace(sq, std::vector<uint8_t>(t + HDR, t + HDR + len)); }
                drain(false); } }
        drain(true); if (pktlog) fclose(pktlog); });
    uint64_t bstat = 0, tstat = now();
    for (;;) { usleep(200000); if (!do_rx && tx_done) break;
        if (now() - tstat > 5000000000ull) { std::cerr << "ONEWAY_STAT t=" << (now() - T0) / 1000000000ull << "s tx_unique=" << tx_unique << " tx_sent=" << tx_sent << " rx_packets=" << rx_pk << " rx_good=" << rx_good
                << " rx_bad=" << rx_bad << " rx_dup=" << rx_dup << " rx_self=" << rx_self << " rx_holes=" << rx_holes << " rx_mbps=" << (rx_bytes - bstat) * 8 / ((now() - tstat) / 1e9) / 1e6 << " out_drops=" << out_drops.load() << std::endl; bstat = rx_bytes; tstat = now(); }
        if (do_rx && rx_done_flag.load()) break; }
    txt.join(); rxthr.join();
    int rc = 0; (void)setup;
    if (do_rx) radio.control->enable_rx_radio(false);
    { std::lock_guard<std::mutex> l(out_m); out_done = true; } out_cv.notify_one();
    if (rec) fclose(rec);
    std::cerr << "ONEWAY_RESULT mode=" << mode << " seconds=" << (now() - T0) / 1e9 << " repeat=" << R << " tx_unique=" << tx_unique << " tx_sent=" << tx_sent << " tx_bursts=" << tx_bursts
              << " rx_packets=" << rx_pk << " rx_good=" << rx_good << " rx_bad=" << rx_bad << " rx_dup=" << rx_dup << " rx_self=" << rx_self << " rx_rearm=" << rx_rearm << " rx_holes=" << rx_holes << " rx_bytes=" << rx_bytes << " out_drops=" << out_drops.load() << " tx_trig_to=" << tx_trig_to.load() << " tx_dropped_bursts=" << tx_dropped_bursts.load() << " tx_fill_bursts=" << tx_fill.load() << " mean_copies=" << (r_n ? (double)r_sum / r_n : 0.0) << std::endl;
    writer.join(); reader.detach();
    return rc;
}
