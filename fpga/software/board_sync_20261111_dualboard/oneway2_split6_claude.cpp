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
        // 2026-11-12 experiments: ONEWAY_SSB_SYNC=<samples> overrides the SSB-detection-to-slot-start distance (library default 10447);
        // ONEWAY_AGC_OFF=1 disables the receive AGC (library default: enabled, vref 800).
        if (getenv("ONEWAY_SSB_SYNC")) radio.cmd->writeReg(RX_SSB_ADDR, (uint32_t)atoi(getenv("ONEWAY_SSB_SYNC")));
        if (getenv("ONEWAY_AGC_OFF")) radio.cmd->writeReg(RX_AGC_ADDR, 800u);
        // 2026-11-12 ROOT CAUSE of the 2-8 % bad packets between two boards: the library sets the receive AGC target (Vref) to 800, which is
        // too low when transmitter and receiver run on different clocks.  Measured Mac->PC, 10 cm, interleaved in one run (~28 000 packets
        // each): Vref 800 -> 1.4-2.0 % bad, 1600 -> 0.01-0.2 %, 2400/3200/4800/6400 -> 0 bad, 9600 -> packets lost.  Default here: 3200.
        // More measurements the same day showed the real cause is the AGC gain changing from packet to packet (it is latched from a 64-sample
        // average at slot detection): any CONSTANT gain gives 0 bad packets.  Which constant depends on the input level: PC-side board as
        // receiver needs the gain pegged at its maximum (AGC on, Vref >= 2400; "AGC off" = gain 1 loses 2-5 % of the packets), the Mac-side
        // board needs gain 1 (AGC off: 0 bad of 33 000; AGC on: 0.6 % bad, Vref >= 1600: 12 %).  So rx-only mode now picks automatically
        // (see ONEWAY_AGC_AUTO below) unless ONEWAY_AGC_OFF=1 or ONEWAY_AGC_VREF=<n> (800 = library behaviour) is given.
        if (!getenv("ONEWAY_AGC_OFF") && getenv("ONEWAY_AGC_VREF")) radio.cmd->writeReg(RX_AGC_ADDR, ((uint32_t)atoi(getenv("ONEWAY_AGC_VREF")) & 0x7FFF) | (1u << 31));
        auto rp = radio.control->get_radio_config();
        // 2026-11-12: the original HELIX two-node examples (throughput_experiment_tx/rx) load the SSB on the transmitter only.
        if (do_tx || !getenv("ONEWAY_NO_SSB")) radio.control->load_SSB(load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));
        { const float fmhz = getenv("HELIX_FREQ_MHZ") ? atof(getenv("HELIX_FREQ_MHZ")) : 400;   // centre frequency (antenna: 1740/1840)
          radio.control->set_freq_band({{fmhz,RFDC_DAC_TYPE,0,0,true},{-fmhz,RFDC_ADC_TYPE,2,0,true}}); }
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
    std::atomic<uint64_t> tx_trig_to{0}, tx_dropped_bursts{0}, tx_unique{0}, tx_sent{0}, tx_bursts{0}, rx_pk{0}, rx_good{0}, rx_bad{0}, rx_dup{0}, rx_holes{0}, rx_bytes{0}; std::atomic<bool> tx_done{!do_tx};
    std::vector<uint8_t> dummy(TXB, 0); dummy[0] = 0xFF; dummy[1] = 0xFF;
    auto paced = [&](size_t B, const std::function<const uint8_t*(size_t)>& get) {     // one TX burst, clock-paced
        static const int test_n = getenv("ONEWAY_TEST_TRIGFAIL") ? atoi(getenv("ONEWAY_TEST_TRIGFAIL")) : 0; static int tcount = 0;   // test hook: pretend every Nth trigger timed out
        if ((test_n && ++tcount % test_n == 0) || !ctrig("triggerTX ", {std::to_string(B * TXB)})) {
            // 2026-11-12: an unconfirmed trigger must NOT be followed by data.  Data and control share the FPGA network input, so
            // untriggered data fills the TX FIFO, the back-pressure then blocks every later control packet, and the transmitter is
            // dead until the bit is reloaded.  Tx-only mode: reset the TX path (full re-init) and trigger again; if that also
            // fails, drop this burst.
            tx_trig_to++; std::cerr << "ONEWAY_WARN TX trigger timeout (" << tx_trig_to.load() << ")" << (do_rx ? "" : ": waiting for a late ACK / retrying") << std::endl;
            if (do_rx) { /* single-board mode: old behaviour */ }
            else { std::lock_guard<std::mutex> l(cmd_m); bool ok = false;
                for (int attempt = 0; attempt < 4 && !ok; attempt++) {
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
    std::thread txt([&] { if (!do_tx) return; uint16_t seq = 0; std::vector<uint8_t> txs; const size_t NU = std::max<size_t>(1, MAXB / R);
        for (;;) { size_t avail; { std::lock_guard<std::mutex> l(in_m); avail = in_buf.size(); }
            if (!avail) { if (in_eof || g_stop) break; usleep(500); continue; }
            if (avail < 32 * PAY && !in_eof) usleep(3000);
            size_t n = 0; { std::lock_guard<std::mutex> l(in_m); size_t off = 0; txs.assign(NU * TBB, 0);
                while (n < NU && off < in_buf.size()) { size_t len = std::min(PAY, in_buf.size() - off); uint8_t* t = txs.data() + n * TBB;
                    memcpy(t + HDR, in_buf.data() + off, len); uint32_t c = crc32(t + HDR, len); uint16_t sq = seq++;
                    t[0] = len & 0xFF; t[1] = len >> 8; t[2] = sq & 0xFF; t[3] = sq >> 8; memcpy(t + 4, &c, 4); off += len; n++; }
                in_buf.erase(in_buf.begin(), in_buf.begin() + off); }
            paced(n * R, [&](size_t k) { return txs.data() + (k % n) * TBB; }); tx_unique += n; tx_sent += n * R; tx_bursts++; }
        tx_done = true; });
    std::atomic<bool> rx_done_flag{!do_rx};
    std::thread rxthr([&] { struct Fin { std::atomic<bool>& f; ~Fin() { f = true; } } fin{rx_done_flag}; if (!do_rx) return; std::vector<uint8_t> rx(RXB), tbv(TBB); bool have = false; uint16_t next = 0; uint64_t t_last = now(), t_stall = now();
        const uint16_t W = getenv("ONEWAY_REORDER") ? (uint16_t)atoi(getenv("ONEWAY_REORDER")) : (uint16_t)MAXB; std::map<uint16_t, std::vector<uint8_t>> pend;
        bool reinit = false; const double wd_s = getenv("ONEWAY_RX_WD_S") ? atof(getenv("ONEWAY_RX_WD_S")) : 0.0;
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
                    if (!do_tx && wd_s > 0 && rx_pk > 0 && !reinit && idle > wd_s) {   // receive-only watchdog: the FPGA receive path sometimes stops delivering mid-batch
                        reinit = true; drain(true); have = false; mark_drop();   /* a restarted sender begins at seq 0 again */ std::cerr << "ONEWAY_WARN no packets for " << idle << " s: re-initialising the receiver" << std::endl; setup(false); break; }
                    continue; }
                k++; rx_pk++; t_last = now(); reinit = false; if (pktlog) { struct { uint64_t t; uint8_t ok; uint8_t z; uint16_t sq; } __attribute__((packed)) e{t_last, 0, 0, 0}; size_t l0 = rx[0] | (rx[1] << 8); std::vector<uint8_t> tb0(TBB); memcpy(tb0.data(), rx.data(), 674); memcpy(tb0.data() + 674, rx.data() + 704, 671); uint32_t c0; memcpy(&c0, tb0.data() + 4, 4); e.ok = (l0 && l0 <= PAY && crc32(tb0.data() + HDR, l0) == c0); e.sq = tb0[2] | (tb0[3] << 8); fwrite(&e, 1, sizeof e, pktlog); } memcpy(tbv.data(), rx.data(), 674); memcpy(tbv.data() + 674, rx.data() + 704, 671);
                const uint8_t* t = tbv.data(); size_t len = t[0] | (t[1] << 8); uint16_t sq = t[2] | (t[3] << 8); uint32_t c; memcpy(&c, t + 4, 4);
                if (len == 0 || len > PAY || crc32(t + HDR, len) != c) { rx_bad++;
                    continue; }
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
    // 2026-11-12 experiment: ONEWAY_FSWEEP="start_kHz:step_kHz:steps:dwell_ms" (rx-only).  While the far transmitter sends continuously,
    // step this board's RX/TX centre frequency (offset from HELIX_FREQ_MHZ) and print packets/bad per step: bad rate versus carrier offset.
    double fs_start = 0, fs_step = 0; int fs_n = 0, fs_dwell = 1000, fs_i = -1; uint64_t fs_t = 0, fs_pk = 0, fs_bad = 0;
    if (!do_tx && getenv("ONEWAY_FSWEEP")) sscanf(getenv("ONEWAY_FSWEEP"), "%lf:%lf:%d:%d", &fs_start, &fs_step, &fs_n, &fs_dwell);
    const double fs_base = getenv("HELIX_FREQ_MHZ") ? atof(getenv("HELIX_FREQ_MHZ")) : 400;
    // ONEWAY_RSWEEP="addr:v1,v2,...:dwell_ms" (rx-only): cycle one FPGA register through the listed values while receiving and print
    // packets/bad per step - interleaved A/B/C comparison inside one run, immune to the slow drift between runs.
    uint32_t rs_addr = 0; std::vector<uint32_t> rs_vals; int rs_dwell = 1000, rs_i = -1; uint64_t rs_t = 0, rs_pk = 0, rs_bad = 0;
    if (!do_tx && getenv("ONEWAY_RSWEEP")) { std::string e = getenv("ONEWAY_RSWEEP"); size_t c1 = e.find(':'), c2 = e.rfind(':');
        rs_addr = std::stoul(e.substr(0, c1), nullptr, 0); rs_dwell = std::stoi(e.substr(c2 + 1));
        std::string v = e.substr(c1 + 1, c2 - c1 - 1); size_t pos = 0;
        while (pos < v.size()) { size_t q = v.find(',', pos); if (q == std::string::npos) q = v.size(); rs_vals.push_back(std::stoul(v.substr(pos, q - pos), nullptr, 0)); pos = q + 1; } }
    // Automatic constant-gain choice (rx-only, default on; ONEWAY_AGC_AUTO=0 disables): once packets arrive, receive 0.5 s with the AGC
    // off (gain 1) and 0.5 s with the gain pegged (AGC on, Vref 3200), then keep whichever delivered more good packets.
    int ag_state = (!do_tx && !getenv("ONEWAY_AGC_OFF") && !getenv("ONEWAY_AGC_VREF") && !getenv("ONEWAY_RSWEEP") && !(getenv("ONEWAY_AGC_AUTO") && atoi(getenv("ONEWAY_AGC_AUTO")) == 0)) ? 0 : 3;
    uint64_t ag_t = 0, ag_pk = 0, ag_bad = 0; long ag_good_off = 0, ag_good_peg = 0; const uint32_t AG_OFF = 800u, AG_PEG = 3200u | (1u << 31);
    auto ag_write = [&](uint32_t v) { std::lock_guard<std::mutex> l(cmd_m); radio.cmd->writeReg(RX_AGC_ADDR, v); };
    for (;;) { usleep((fs_n || !rs_vals.empty()) ? 20000 : 200000); if (!do_rx && tx_done) break;
        if (ag_state == 0 && rx_pk > 0) { ag_write(AG_OFF); usleep(40000); ag_t = now(); ag_pk = rx_pk; ag_bad = rx_bad; ag_state = 1; }
        else if (ag_state == 1 && now() - ag_t >= 500000000ull) { ag_good_off = (long)(rx_pk - ag_pk) - (long)(rx_bad - ag_bad); ag_write(AG_PEG); usleep(40000); ag_t = now(); ag_pk = rx_pk; ag_bad = rx_bad; ag_state = 2; }
        else if (ag_state == 2 && now() - ag_t >= 500000000ull) { ag_good_peg = (long)(rx_pk - ag_pk) - (long)(rx_bad - ag_bad);
            const bool off = ag_good_off > ag_good_peg; if (off) ag_write(AG_OFF); ag_state = 3;
            std::cerr << "ONEWAY_AGC_AUTO good_with_gain1=" << ag_good_off << " good_with_gain_pegged=" << ag_good_peg << " -> " << (off ? "AGC off (gain 1)" : "gain pegged (Vref 3200)") << std::endl; }
        if (!rs_vals.empty() && rx_pk > 0 && now() - rs_t >= (uint64_t)rs_dwell * 1000000ull) {
            if (rs_i >= 0) std::cerr << "RSWEEP val=" << rs_vals[rs_i % rs_vals.size()] << " packets=" << rx_pk - rs_pk << " bad=" << rx_bad - rs_bad << std::endl;
            rs_i++; { std::lock_guard<std::mutex> l(cmd_m); radio.cmd->writeReg(rs_addr, rs_vals[rs_i % rs_vals.size()]); }
            usleep(30000); rs_t = now(); rs_pk = rx_pk; rs_bad = rx_bad; }
        if (fs_n && rx_pk > 0 && now() - fs_t >= (uint64_t)fs_dwell * 1000000ull) {
            if (fs_i >= 0) std::cerr << "FSWEEP off_kHz=" << fs_start + fs_step * (fs_i % fs_n) << " packets=" << rx_pk - fs_pk << " bad=" << rx_bad - fs_bad << std::endl;
            fs_i++; const float f = fs_base + (fs_start + fs_step * (fs_i % fs_n)) / 1000.0;
            { std::lock_guard<std::mutex> l(cmd_m); radio.control->set_freq_band({{f,RFDC_DAC_TYPE,0,0,true},{-f,RFDC_ADC_TYPE,2,0,true}}); }
            usleep(30000); fs_t = now(); fs_pk = rx_pk; fs_bad = rx_bad; }
        if (now() - tstat > 5000000000ull) { std::cerr << "ONEWAY_STAT t=" << (now() - T0) / 1000000000ull << "s tx_unique=" << tx_unique << " tx_sent=" << tx_sent << " rx_packets=" << rx_pk << " rx_good=" << rx_good
                << " rx_bad=" << rx_bad << " rx_dup=" << rx_dup << " rx_holes=" << rx_holes << " rx_mbps=" << (rx_bytes - bstat) * 8 / ((now() - tstat) / 1e9) / 1e6 << " out_drops=" << out_drops.load() << std::endl; bstat = rx_bytes; tstat = now(); }
        if (do_rx && rx_done_flag.load()) break; }
    txt.join(); rxthr.join();
    int rc = 0; (void)setup;
    if (do_rx) radio.control->enable_rx_radio(false);
    { std::lock_guard<std::mutex> l(out_m); out_done = true; } out_cv.notify_one();
    if (rec) fclose(rec);
    std::cerr << "ONEWAY_RESULT mode=" << mode << " seconds=" << (now() - T0) / 1e9 << " repeat=" << R << " tx_unique=" << tx_unique << " tx_sent=" << tx_sent << " tx_bursts=" << tx_bursts
              << " rx_packets=" << rx_pk << " rx_good=" << rx_good << " rx_bad=" << rx_bad << " rx_dup=" << rx_dup << " rx_holes=" << rx_holes << " rx_bytes=" << rx_bytes << " out_drops=" << out_drops.load() << " tx_trig_to=" << tx_trig_to.load() << " tx_dropped_bursts=" << tx_dropped_bursts.load() << std::endl;
    writer.join(); reader.detach();
    return rc;
}
