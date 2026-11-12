// Claude 2026-11-11: N_RE=73 QPSK r=490/1024 soak over the original HELIX host library (UDP path).
// usage: soak_nre73_claude <tx_split73_file> <expected_split6_file> <first_packet> <count> [tx_split 2|4] [tx_bytes_per_packet]
// TX file: concatenated packets (tx_bytes each, no flush); expected: 1408 B per packet. 3x64-bit zero flush is appended per packet.
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cmath>
#include <chrono>
#include <unistd.h>
const char* fpga_ip = "192.168.10.2";
static std::vector<uint8_t> slurp(const char* fn){ std::ifstream f(fn, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
int main(int argc, char** argv){
    if (argc < 5) { std::cerr << "args\n"; return 2; }
    auto tx = slurp(argv[1]); auto exp = slurp(argv[2]);
    size_t first = std::stoul(argv[3]), count = std::stoul(argv[4]);
    uint8_t tx_split = argc > 5 ? std::stoi(argv[5]) : SPLIT_7_3;
    size_t TXB = argc > 6 ? std::stoul(argv[6]) : 2632; const size_t RXB = 1408, FLUSH = 24; long gap_us = argc > 7 ? std::stol(argv[7]) : 0;
    size_t avail = std::min(tx.size()/TXB, exp.size()/RXB);
    if (first + count > avail) { std::cerr << "only " << avail << " packets in files\n"; return 2; }
    auto radio = helix::helix(fpga_ip);
    helix::stream_str sc{}; sc.udp_rx_mss = 1024*8; sc.udp_rx_ifg = sc.udp_rx_mss/10; sc.radio_tx_mss = pow(2,32)*8-1; sc.radio_tx_ifg = 0;
    radio.control->set_streaming_param(sc);
    radio.control->configure_radio(SPLIT_6, tx_split, BW_MODE_HIGH, 73, MOD_QPSK, 490.0/1024, 0);
    auto rp = radio.control->get_radio_config();
    std::vector<int16_t> ssb = load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt");
    radio.control->load_SSB(ssb);
    radio.control->set_freq_band({{400,RFDC_DAC_TYPE,0,0,true},{-400,RFDC_ADC_TYPE,2,0,true}});
    uint32_t nrx = radio.control->get_num_of_rx_bytes(SPLIT_6);
    if (nrx != RXB) { std::cerr << "unexpected rx bytes " << nrx << "\n"; return 2; }
    helix::slot_str rx(nrx, rp->ofdm.num_sc*4);
    std::vector<uint8_t> pkt(TXB + FLUSH, 0);
    usleep(10000);
    memcpy(pkt.data(), tx.data() + first*TXB, TXB);
    radio.stream->transmit(pkt.data(), pkt.size());          // warm-up slot, as in the original loopback example
    usleep(20000);                                           // the warm-up slot must be over before RX is enabled, otherwise it is
                                                             // captured (fast bridge) and every later result is one packet behind
    radio.control->enable_rx_radio(true);
    size_t ok = 0, bad = 0, lost = 0, biterr = 0; long first_bad = -1;
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = first; i < first + count; i++) {
        memcpy(pkt.data(), tx.data() + i*TXB, TXB);
        rx.data.assign(nrx, 0); if (gap_us) usleep(gap_us);
        radio.stream->transmit(pkt.data(), pkt.size());
        radio.stream->receive(&rx, nrx, false, false, false);
        if (rx.data.size() != nrx) { lost++; if (first_bad < 0) first_bad = i; continue; }
        if (!memcmp(rx.data.data(), exp.data() + i*RXB, RXB)) ok++;
        else { bad++; if (first_bad < 0) first_bad = i; for (size_t k = 0; k < RXB; k++) biterr += __builtin_popcount(rx.data[k] ^ exp[i*RXB + k]); }
    }
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    radio.control->enable_rx_radio(false);
    std::cout << "SOAK_RESULT packets=" << count << " exact=" << ok << " mismatch=" << bad << " lost=" << lost << " bit_errors=" << biterr
              << " first_bad=" << first_bad << " seconds=" << dt << " ms_per_packet=" << 1000*dt/count
              << " decoded_mbps=" << (ok*RXB*8/dt/1e6) << std::endl;
    return (ok == count) ? 0 : 1;
}
