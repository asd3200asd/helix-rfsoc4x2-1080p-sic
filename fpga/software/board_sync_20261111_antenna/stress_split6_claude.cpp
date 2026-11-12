// Claude 2026-11-11: TX split-6 goodput stress test (helix_nlsg_s6.bit), N_RE=73 QPSK r=490/1024: raw transport blocks are
// supplied continuously from memory; every received Split-6 packet is compared byte-for-byte with the expected one.
// usage: stress_split6_claude <tb_file> <expected_file> <burst_size> <bursts> [tx_ifg_bytes=8] [send_gap_us=0] [rx_ifg_bytes=128] [window=64]
//   One burst = one triggerRX(burst*1408, burst) + one triggerTX(burst*1352) followed by <burst_size> datagrams; a receiver
//   thread drains the RX socket concurrently.  radio_tx_mss = 1352 so the Stream Manager closes each TB with TLAST;
//   udp_rx_mss = 1408 so every decoded packet is its own datagram.
// Output: STRESS_RESULT ... goodput_mbps = exact TBs * 1345 * 8 / wall time (transport-block payload, CRC excluded).
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include "helix_split6.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <cmath>
#include <ctime>
#include <unistd.h>
const char* fpga_ip = "192.168.10.2";
static uint64_t now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static std::vector<uint8_t> slurp(const char* fn){ std::ifstream f(fn, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static bool trig(helix::helix& r, const char* name, std::vector<std::string> args) { helix::cmd_struct c; c.cmd = name; c.cmdArgs = args; return r.cmd->sendCmd(c, true) == UDP_CMD_ACK; }
int main(int argc, char** argv){
    if (argc < 5) { std::cerr << "args\n"; return 2; }
    auto tb = slurp(argv[1]); auto exp = slurp(argv[2]); const size_t B = std::stoul(argv[3]), NB = std::stoul(argv[4]);
    // Stream Manager quirk (packet_fragmentation.v): between MSS segments it waits until ifg_count >= ifg-1, so ifg = 0 words
    // means 2^32 clock cycles.  Both gaps must therefore be at least one 64-bit word (8 bytes) in burst mode.
    const uint32_t ifg = argc > 5 ? std::stoul(argv[5]) : 8; const long send_gap = argc > 6 ? std::stol(argv[6]) : 0; const uint32_t rx_ifg = argc > 7 ? std::stoul(argv[7]) : 128;
    // Flow control: UDP has none and the FPGA ingress drops frames when its buffers fill (seen with the fast bridge: 256 TBs
    // pushed at once -> 94 lost, Stream Manager left waiting).  The sender therefore never has more than <window> TBs
    // outstanding (sent but not yet received back).
    const size_t window = argc > 8 ? std::stoul(argv[8]) : 64;
    const size_t TBB = 1345, TXB = 1352, RXB = 1408, NTB = std::min(tb.size() / TBB, exp.size() / RXB);
    auto radio = helix::helix(fpga_ip);
    helix::stream_str sc{}; sc.udp_rx_mss = RXB; sc.udp_rx_ifg = rx_ifg; sc.radio_tx_mss = TXB; sc.radio_tx_ifg = ifg;
    radio.control->set_streaming_param(sc);
    radio.control->configure_radio(SPLIT_6, SPLIT_6, BW_MODE_HIGH, 73, MOD_QPSK, 490.0/1024, 0);
    auto rp = radio.control->get_radio_config();
    radio.control->load_SSB(load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));
    { const float fmhz = getenv("HELIX_FREQ_MHZ") ? atof(getenv("HELIX_FREQ_MHZ")) : 400;   // centre frequency (antenna: 1740/1840)
          radio.control->set_freq_band({{fmhz,RFDC_DAC_TYPE,0,0,true},{-fmhz,RFDC_ADC_TYPE,2,0,true}}); }
    if (!split6::enable(radio, rp->tbs, rp->num_sch * MOD_QPSK, MOD_QPSK)) return 2;
    std::vector<uint8_t> pkt(TXB, 0);
    usleep(10000);
    memcpy(pkt.data(), tb.data(), TBB);
    if (!trig(radio, "triggerTX ", {std::to_string(TXB)})) return 3;
    radio.udp->data_socket.send(pkt.data(), TXB);            // warm-up slot
    usleep(20000);                                           // the warm-up slot must be over before RX is enabled, otherwise it is
                                                             // captured (fast bridge) and every later result is one packet behind
    radio.control->enable_rx_radio(true);
    const uint32_t enc0 = split6::packets_done(radio);
    size_t exact = 0, wrong = 0, lost = 0, bursts_all_ok = 0; uint64_t biterr = 0, busy = 0; size_t idx = 0;
    const uint64_t T0 = now();
    for (size_t b = 0; b < NB; b++) {
        const size_t base = idx; std::atomic<size_t> r_ok{0}, r_bad{0}, r_got{0}; uint64_t be = 0;
        const uint64_t t0 = now();
        if (!trig(radio, "triggerRX ", {std::to_string(B * RXB), "0", "0", "0", std::to_string(B)})) { std::cerr << "RX trigger failed\n"; break; }
        std::thread rxt([&] { std::vector<uint8_t> rx(RXB);
            for (size_t k = 0; k < B; k++) { ssize_t n = radio.udp->data_socket.recv(rx.data(), RXB); if (n != (ssize_t)RXB) break; r_got++;
                const uint8_t* e = exp.data() + ((base + k) % NTB) * RXB;
                if (!memcmp(rx.data(), e, RXB)) r_ok++; else { r_bad++; for (size_t j = 0; j < RXB; j++) be += __builtin_popcount(rx[j] ^ e[j]); } } });
        if (!trig(radio, "triggerTX ", {std::to_string(B * TXB)})) { std::cerr << "TX trigger failed\n"; }
        if (getenv("STRESS_DBG")) std::cerr << "DBG triggers done t_ms=" << (now()-t0)/1e6 << std::endl;
        for (size_t k = 0; k < B; k++) {
            uint64_t tw = now();
            while (k - r_got.load() >= window && now() - tw < 300000000ull) usleep(20);
            memcpy(pkt.data(), tb.data() + ((base + k) % NTB) * TBB, TBB); radio.udp->data_socket.send(pkt.data(), TXB); if (send_gap) usleep(send_gap); }
        if (getenv("STRESS_DBG")) std::cerr << "DBG sent all, r_got=" << r_got.load() << " t_ms=" << (now()-t0)/1e6 << std::endl;
        rxt.join(); busy += now() - t0; idx += B;
        if (getenv("STRESS_DBG")) std::cerr << "DBG joined r_got=" << r_got.load() << " ok=" << r_ok.load() << " t_ms=" << (now()-t0)/1e6 << std::endl;
        exact += r_ok; wrong += r_bad; lost += B - r_got; biterr += be; bursts_all_ok += (r_ok == B);
        if (r_got != B) usleep(400000);                         // let stragglers drain before the next burst
    }
    const double wall = (now() - T0) / 1e9, bz = busy / 1e9;
    radio.control->enable_rx_radio(false);
    const uint32_t enc1 = split6::packets_done(radio);
    std::cout << "STRESS_RESULT burst=" << B << " bursts=" << NB << " sent=" << NB * B << " exact=" << exact << " wrong=" << wrong << " lost=" << lost << " bit_errors=" << biterr
              << " bursts_all_ok=" << bursts_all_ok << " encoder_done=" << (enc1 - enc0) << " busy_s=" << bz << " ms_per_tb=" << 1000 * bz / (NB * B)
              << " goodput_mbps=" << exact * TBB * 8 / bz / 1e6 << " tx_ifg=" << ifg << " rx_ifg=" << rx_ifg << " window=" << window << std::endl;
    return (exact == NB * B) ? 0 : 1;
}
