// Claude 2026-11-11: continuous byte-stream relay over the HELIX link in burst mode (helix_nlsg_s6v2.bit, TX/RX split 6,
// N_RE=73 QPSK r=490/1024).  stdin bytes -> transport blocks -> FPGA LDPC encoder -> DAC B -> ADC B -> PHY -> stdout bytes.
// usage: stream_relay_split6_claude [tx_ifg=180000] [window=32] [max_burst=256] [record_file]
// TB layout (1345 B): u16 len | u16 seq | u32 crc32(payload[0..len)) | payload (<= 1337 B).  Each burst is one
// triggerRX(N*1408, N) + triggerTX(N*1352) + N datagrams with a flow window.  Received TB = rx[0..674) + rx[704..704+671).
// Reliability: burst-level go-back-N resend + full re-init on control time-out / incomplete burst (STREAM_FAULT=<burst#>
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
        radio.control->set_freq_band({{400,RFDC_DAC_TYPE,0,0,true},{-400,RFDC_ADC_TYPE,2,0,true}});
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
    // Burst-level go-back-N: TBs are delivered strictly in order; everything from the first missing / CRC-bad / out-of-sequence
    // TB of a burst onward is sent again in the next burst.  Nothing is ever dropped silently.
    uint64_t tbs = 0, bytes = 0, bursts = 0, bad_crc = 0, missing = 0, retx = 0, recov = 0, trig_to = 0; uint16_t seq = 0; int consec = 0, zero = 0, rc = 0;
    const char* fe = getenv("STREAM_FAULT"); const uint64_t fault_at = fe ? std::stoull(fe) : 0; const int fault_kind = getenv("STREAM_FAULT_KIND") ? atoi(getenv("STREAM_FAULT_KIND")) : 1;
    const uint64_t T0 = now(); uint64_t tstat = T0, bstat = 0;
    std::vector<uint8_t> chunk, txs;
    for (;;) {
        size_t avail; { std::lock_guard<std::mutex> l(in_m); avail = in_buf.size(); }
        if (chunk.empty() && !avail) { if (in_eof) break; usleep(500); continue; }
        if (chunk.size() + avail < 32 * PAY && !in_eof) usleep(3000);
        { std::lock_guard<std::mutex> l(in_m); size_t room = chunk.size() < MAXB * PAY ? MAXB * PAY - chunk.size() : 0; size_t n = std::min(in_buf.size(), room);
          chunk.insert(chunk.end(), in_buf.begin(), in_buf.begin() + n); in_buf.erase(in_buf.begin(), in_buf.begin() + n); }
        const size_t B = (chunk.size() + PAY - 1) / PAY; txs.assign(B * TBB, 0);
        for (size_t k = 0; k < B; k++) { uint8_t* t = txs.data() + k * TBB; size_t len = std::min(PAY, chunk.size() - k * PAY); uint16_t sq = seq + k;
            memcpy(t + HDR, chunk.data() + k * PAY, len); uint32_t c = crc32(t + HDR, len);
            t[0] = len & 0xFF; t[1] = len >> 8; t[2] = sq & 0xFF; t[3] = sq >> 8; memcpy(t + 4, &c, 4); }
        bursts++; const bool fault = fault_at && bursts == fault_at;
        std::atomic<size_t> r_got{0}; std::vector<uint8_t> rxall(B * TBB); bool ctl_ok = true;
        if (!trig(radio, "triggerRX ", {std::to_string(B * RXB), "0", "0", "0", std::to_string(B)})) { ctl_ok = false; trig_to++; std::cerr << "RX trigger timeout\n"; }
        if (ctl_ok) {
            std::thread rxt([&] { std::vector<uint8_t> rx(RXB);
                for (size_t k = 0; k < B; k++) { ssize_t n = radio.udp->data_socket.recv(rx.data(), RXB); if (n != (ssize_t)RXB) break;
                    memcpy(rxall.data() + k * TBB, rx.data(), 674); memcpy(rxall.data() + k * TBB + 674, rx.data() + 704, 671); r_got++; } });
            if (fault && fault_kind == 1) { std::cerr << "FAULT_INJECTED: TX trigger skipped\n"; ctl_ok = false; }
            else if (!trig(radio, "triggerTX ", {std::to_string(B * TXB)})) { ctl_ok = false; trig_to++; std::cerr << "TX trigger timeout\n"; }
            if (ctl_ok || fault) for (size_t k = 0; k < B; k++) { uint64_t tw = now();
                while (k - r_got.load() >= window && now() - tw < 300000000ull) usleep(20);
                if (fault && fault_kind == 2 && k == B / 2) { std::cerr << "FAULT_INJECTED: datagram withheld\n"; continue; }
                memcpy(pkt.data(), txs.data() + k * TBB, TBB); radio.udp->data_socket.send(pkt.data(), TXB); }
            rxt.join(); }
        const size_t G = r_got.load(); size_t good = 0;
        for (; good < G; good++) { const uint8_t* t = rxall.data() + good * TBB; size_t len = t[0] | (t[1] << 8); uint16_t sq = t[2] | (t[3] << 8); uint32_t c; memcpy(&c, t + 4, 4);
            if (len > PAY || sq != (uint16_t)(seq + good) || crc32(t + HDR, len) != c) { bad_crc++; break; }
            tbs++; bytes += len; emit(t + HDR, len); }
        seq += good; chunk.erase(chunk.begin(), chunk.begin() + std::min(chunk.size(), good * PAY));
        if (good == B) { consec = 0; zero = 0; }
        else { retx += B - good; missing += B - G; zero = good ? 0 : zero + 1;
            if (G != B || !ctl_ok) { recov++; std::cerr << "BURST_INCOMPLETE got=" << G << " good=" << good << " of " << B << " -> recover + resend" << std::endl;
                bool ok = false; for (int a = 0; a < 3 && !ok; a++) ok = setup(false);
                if (!ok || ++consec >= 5) { std::cerr << "STREAM_FATAL recovery failed (bit reload needed)" << std::endl; rc = 3; break; } }
            if (zero >= 30) { std::cerr << "STREAM_FATAL 30 bursts in a row without a single good TB" << std::endl; rc = 4; break; } }
        if (now() - tstat > 5000000000ull) { size_t q; { std::lock_guard<std::mutex> l(in_m); q = in_buf.size(); } size_t oq; { std::lock_guard<std::mutex> l(out_m); oq = out_bytes; }
            std::cerr << "STREAM_STAT t=" << (now() - T0) / 1000000000ull << "s tbs=" << tbs << " bytes=" << bytes << " mbps=" << (bytes - bstat) * 8 / ((now() - tstat) / 1e9) / 1e6
                      << " bursts=" << bursts << " bad=" << bad_crc << " missing=" << missing << " retx=" << retx << " recov=" << recov << " trig_to=" << trig_to << " in_q=" << q << " out_q=" << oq << " out_drops=" << out_drops.load() << std::endl;
            tstat = now(); bstat = bytes; }
    }
    radio.control->enable_rx_radio(false);
    { std::lock_guard<std::mutex> l(out_m); out_done = true; } out_cv.notify_one();
    if (rec) fclose(rec);
    std::cerr << "STREAM_RESULT seconds=" << (now() - T0) / 1e9 << " tbs=" << tbs << " bytes=" << bytes << " avg_mbps=" << bytes * 8 / ((now() - T0) / 1e9) / 1e6 << " bursts=" << bursts
              << " bad=" << bad_crc << " missing=" << missing << " retx=" << retx << " recov=" << recov << " trig_to=" << trig_to << " undelivered=" << chunk.size() << " out_drops=" << out_drops.load() << " rc=" << rc << std::endl;
    writer.join(); reader.detach();
    return rc;
}
