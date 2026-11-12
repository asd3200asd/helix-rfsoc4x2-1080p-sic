// Claude 2026-11-11: continuous byte-stream relay over the HELIX link in burst mode (helix_nlsg_s6v2.bit, TX/RX split 6,
// N_RE=73 QPSK r=490/1024).  stdin bytes -> transport blocks -> FPGA LDPC encoder -> DAC B -> ADC B -> PHY -> stdout bytes.
// usage: stream_relay_split6_claude [tx_ifg=180000] [window=32] [max_burst=256] [record_file]
// TB layout (1345 B): u16 len | u16 seq | u32 crc32(payload[0..len)) | payload (<= 1337 B).  Each burst is one
// triggerRX(N*1408, N) + triggerTX(N*1352) + N datagrams with a flow window.  Received TB = rx[0..674) + rx[704..704+671).
// Reliability: selective-repeat resend + full re-init on control time-out / incomplete burst (STREAM_FAULT=<burst#>
// STREAM_FAULT_KIND=1|2 injects a skipped TX trigger / a withheld datagram for testing).  rc 3/4 = unrecoverable.
// stdout never blocks the radio: a writer thread owns it; if the consumer (video decoder) falls behind by > OUT_MAX bytes
// the queue is dropped and output resumes at the next H.264 SPS (start of a GOP).  Stats go to stderr every 5 s.
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include "helix_split6.h"
#include <iostream>
#include <vector>
#include <deque>
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
static bool trig(helix::helix& r, const char* name, std::vector<std::string> args) { helix::helix& R_ = r; helix::cmd_struct c; c.cmd = name; c.cmdArgs = args;
    if (R_.cmd->sendCmd(c, true) == UDP_CMD_ACK) return true;
    // 2026-11-12 (Claude): never report "not confirmed" lightly.  Data and control share the FPGA network input: data sent after an
    // unconfirmed triggerTX fills the TX FIFO, blocks every later control packet and the board needs a bit reload.  The control
    // service (Python) sometimes answers later than the 200 ms socket time-out, so wait for the late ACK, then retry.
    for (int attempt = 0; attempt < 3; attempt++) { uint32_t late = 0; bool got = false;
        for (int i = 0; i < 3 && !got; i++) got = R_.udp->control_socket.recv(&late, sizeof late) > 0;
        if (got && late == UDP_CMD_ACK) return true;
        if (R_.cmd->sendCmd(c, true) == UDP_CMD_ACK) return true; }
    return false; }
static const size_t TBB = 1345, TXB = 1352, RXB = 1408, HDR = 8, PAY = TBB - HDR, IN_MAX = 16u << 20, OUT_MAX = 6u << 20;
static std::mutex in_m; static std::vector<uint8_t> in_buf; static std::atomic<bool> in_eof{false};
static std::mutex out_m; static std::condition_variable out_cv; static std::deque<std::vector<uint8_t>> out_q; static size_t out_bytes = 0; static bool out_done = false;
static std::atomic<uint64_t> out_drops{0};
int main(int argc, char** argv){
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
        radio.control->load_SSB(load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));
        { const float fmhz = getenv("HELIX_FREQ_MHZ") ? atof(getenv("HELIX_FREQ_MHZ")) : 400;   // centre frequency (antenna: 1740/1840)
          radio.control->set_freq_band({{fmhz,RFDC_DAC_TYPE,0,0,true},{-fmhz,RFDC_ADC_TYPE,2,0,true}}); }
        if (!split6::enable(radio, rp->tbs, rp->num_sch * MOD_QPSK, MOD_QPSK)) return false;
        usleep(10000); memset(pkt.data(), 0, TXB);
        if (!trig(radio, "triggerTX ", {std::to_string(TXB)})) return false;
        radio.udp->data_socket.send(pkt.data(), TXB);        // warm-up slot (must be over before RX is enabled)
        usleep(20000);
        radio.control->enable_rx_radio(true);
        return true; };
    if (!setup(true)) return 2;
    std::cerr << "STREAM_RELAY_READY ifg=" << ifg << " window=" << window << " max_burst=" << MAXB << std::endl;
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
    // Selective repeat: every TB carries seq + CRC, so any TB that arrives intact is accepted wherever it lands in the burst;
    // only the TBs that are missing or CRC-bad are sent again (first in the next burst).  Output is strictly in sequence.
    struct TB { uint16_t seq; std::vector<uint8_t> d; bool acked; };
    std::deque<TB> pend; std::map<uint16_t, std::vector<uint8_t>> done; uint16_t seq_new = 0, seq_emit = 0;
    uint64_t tbs = 0, bytes = 0, bursts = 0, bad_crc = 0, missing = 0, retx = 0, recov = 0, trig_to = 0, sent_tbs = 0; int consec = 0, zero = 0, rc = 0;
    const char* fe = getenv("STREAM_FAULT"); const uint64_t fault_at = fe ? std::stoull(fe) : 0; const int fault_kind = getenv("STREAM_FAULT_KIND") ? atoi(getenv("STREAM_FAULT_KIND")) : 1;
    const uint64_t T0 = now(); uint64_t tstat = T0, bstat = 0;
    std::vector<uint8_t> txs;
    for (;;) {
        size_t avail; { std::lock_guard<std::mutex> l(in_m); avail = in_buf.size(); }
        if (pend.empty() && !avail) { if (in_eof) break; usleep(500); continue; }
        if (pend.size() * PAY + avail < 32 * PAY && !in_eof) usleep(3000);
        { std::lock_guard<std::mutex> l(in_m); size_t off = 0;
          while (pend.size() < MAXB && off < in_buf.size() && (uint16_t)(seq_new - seq_emit) < 20000) { size_t len = std::min(PAY, in_buf.size() - off);
              pend.push_back({seq_new++, std::vector<uint8_t>(in_buf.begin() + off, in_buf.begin() + off + len), false}); off += len; }
          in_buf.erase(in_buf.begin(), in_buf.begin() + off); }
        const size_t B = pend.size(); txs.assign(B * TBB, 0);
        for (size_t k = 0; k < B; k++) { uint8_t* t = txs.data() + k * TBB; const TB& b = pend[k]; size_t len = b.d.size();
            memcpy(t + HDR, b.d.data(), len); uint32_t c = crc32(t + HDR, len);
            t[0] = len & 0xFF; t[1] = len >> 8; t[2] = b.seq & 0xFF; t[3] = b.seq >> 8; memcpy(t + 4, &c, 4); }
        bursts++; sent_tbs += B; const bool fault = fault_at && bursts == fault_at;
        // The Stream Manager (packet_fragmentation.v) leaves SEND only after the programmed number of words has passed and it
        // ignores triggers while in SEND, so a stalled burst can only be cleared by completing it:
        //   phase 1: triggers that may not have been executed are issued again (harmless when they were) and the remaining real
        //            TBs are fed;  phase 2: for every RX packet still owed (slot lost on air / datagram lost) a dummy TB is fed,
        //            first without a trigger (TX side still in SEND), then with a trigger of exactly that size.
        std::atomic<size_t> r_got{0}; std::vector<uint8_t> rxall(B * TBB); bool ctl_ok = true; bool stalled = false;
        auto rx_run = [&] { std::vector<uint8_t> rx(RXB);
            while (r_got.load() < B) { ssize_t n = radio.udp->data_socket.recv(rx.data(), RXB); if (n != (ssize_t)RXB) break; size_t k = r_got.load();
                memcpy(rxall.data() + k * TBB, rx.data(), 674); memcpy(rxall.data() + k * TBB + 674, rx.data() + 704, 671); r_got++; } };
        std::vector<uint8_t> dummy(TXB, 0); dummy[0] = 0xFF; dummy[1] = 0xFF;      // len 0xFFFF -> rejected by the receiver side
        size_t credit = 0;   // TBs written off as lost on air (a run of >= window lost slots would otherwise block the feed forever)
        auto feed = [&](size_t from, size_t to, bool real, size_t base) -> size_t { size_t k = from;
            for (; k < to; k++) { uint64_t tw = now();
                auto ahead = [&] { size_t g = r_got.load() + credit; return base + k > g && base + k - g >= window; };   // (a stale packet can make g > base+k)
                while (ahead() && now() - tw < 300000000ull) usleep(20);
                if (ahead()) { stalled = true; break; }
                if (real && fault && fault_kind == 2 && k == B / 2) { std::cerr << "FAULT_INJECTED: datagram withheld\n"; continue; }
                if (real) { memcpy(pkt.data(), txs.data() + k * TBB, TBB); radio.udp->data_socket.send(pkt.data(), TXB); } else radio.udp->data_socket.send(dummy.data(), TXB); }
            return k; };
        const std::vector<std::string> rxargs = {std::to_string(B * RXB), "0", "0", "0", std::to_string(B)};
        if (!trig(radio, "triggerRX ", rxargs)) { ctl_ok = false; trig_to++; std::cerr << "RX trigger timeout\n"; }
        size_t fed = 0; bool tx_untriggered = false;
        { std::thread rxt(rx_run);
          if (fault && fault_kind == 1) { std::cerr << "FAULT_INJECTED: TX trigger skipped\n"; ctl_ok = false; }
          else if (!trig(radio, "triggerTX ", {std::to_string(B * TXB)})) { ctl_ok = false; trig_to++; tx_untriggered = true; std::cerr << "TX trigger timeout\n"; }
          fed = tx_untriggered ? 0 : feed(0, B, true, 0); rxt.join(); }   // 2026-11-12: no data without a confirmed trigger
        if (r_got.load() < B) { recov++; std::cerr << "BURST_STALLED got=" << r_got.load() << " fed=" << fed << " of " << B << " -> completing" << std::endl;
            for (int a = 0; a < 12 && fed < B; a++) {                               // phase 1
                if (r_got.load() == 0 && a == 0) { trig(radio, "triggerRX ", rxargs); trig(radio, "triggerTX ", {std::to_string(B * TXB)}); }
                else if (fed > r_got.load()) credit = fed - r_got.load();          // outstanding slots were lost on air: write them off, keep feeding
                stalled = false; std::thread rxt(rx_run); fed = feed(fed, B, true, 0); rxt.join(); }
            credit = 0;
            for (int a = 0; a < 8 && fed == B && r_got.load() < B; a++) {           // phase 2
                const size_t g0 = r_got.load(), m = B - g0;
                { stalled = false; std::thread rxt(rx_run); feed(0, m, false, g0); rxt.join(); }
                if (r_got.load() == g0) { trig(radio, "triggerTX ", {std::to_string(m * TXB)}); std::thread rxt(rx_run); rxt.join(); } }
            if (r_got.load() < B) { std::cerr << "STREAM_FATAL burst could not be completed (got " << r_got.load() << " of " << B << "; bit reload needed)" << std::endl; rc = 3; break; }
            std::cerr << "BURST_COMPLETED" << std::endl; }
        const size_t G = r_got.load(); size_t good = 0;
        for (size_t k = 0; k < G; k++) { const uint8_t* t = rxall.data() + k * TBB; size_t len = t[0] | (t[1] << 8); uint16_t sq = t[2] | (t[3] << 8); uint32_t c; memcpy(&c, t + 4, 4);
            if (len > PAY || crc32(t + HDR, len) != c) { bad_crc++; continue; }
            uint16_t idx = sq - pend.front().seq;                       // pend is contiguous-in-order but may have gaps removed -> search
            TB* hit = nullptr; if (idx < pend.size() && pend[idx].seq == sq) hit = &pend[idx]; else for (auto& b : pend) if (b.seq == sq) { hit = &b; break; }
            if (!hit || hit->acked || hit->d.size() != len) continue;
            hit->acked = true; good++; done[sq] = std::move(hit->d); }
        for (auto it = pend.begin(); it != pend.end();) it = it->acked ? pend.erase(it) : it + 1;
        for (auto it = done.find(seq_emit); it != done.end(); it = done.find(seq_emit)) { tbs++; bytes += it->second.size(); emit(it->second.data(), it->second.size()); done.erase(it); seq_emit++; }
        (void)consec; (void)stalled; (void)setup;
        if (good == B) { zero = 0; }
        else { retx += B - good; missing += B - G; zero = good ? 0 : zero + 1;
            if (zero >= 30) { std::cerr << "STREAM_FATAL 30 bursts in a row without a single good TB" << std::endl; rc = 4; break; } }
        if (now() - tstat > 5000000000ull) { size_t q; { std::lock_guard<std::mutex> l(in_m); q = in_buf.size(); } size_t oq; { std::lock_guard<std::mutex> l(out_m); oq = out_bytes; }
            std::cerr << "STREAM_STAT t=" << (now() - T0) / 1000000000ull << "s tbs=" << tbs << " bytes=" << bytes << " mbps=" << (bytes - bstat) * 8 / ((now() - tstat) / 1e9) / 1e6
                      << " bursts=" << bursts << " sent=" << sent_tbs << " bad=" << bad_crc << " missing=" << missing << " retx=" << retx << " recov=" << recov << " trig_to=" << trig_to << " in_q=" << q << " out_q=" << oq << " out_drops=" << out_drops.load() << std::endl;
            tstat = now(); bstat = bytes; }
    }
    radio.control->enable_rx_radio(false);
    { std::lock_guard<std::mutex> l(out_m); out_done = true; } out_cv.notify_one();
    if (rec) fclose(rec);
    std::cerr << "STREAM_RESULT seconds=" << (now() - T0) / 1e9 << " tbs=" << tbs << " bytes=" << bytes << " avg_mbps=" << bytes * 8 / ((now() - T0) / 1e9) / 1e6 << " bursts=" << bursts
              << " bad=" << bad_crc << " missing=" << missing << " retx=" << retx << " recov=" << recov << " trig_to=" << trig_to << " sent=" << sent_tbs << " undelivered_tbs=" << pend.size() + done.size() << " out_drops=" << out_drops.load() << " rc=" << rc << std::endl;
    writer.join(); reader.detach();
    return rc;
}
