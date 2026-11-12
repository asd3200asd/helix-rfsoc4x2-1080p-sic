// Claude 2026-11-11: N_RE=73 QPSK r=490/1024, TX split 7.3 -> selectable RX split, dump raw RX bytes for offline check.
// usage: splitcap_param_claude <tx_split73_file> <first> <count> <rx_split 1..5> <out_file> <gap_us> <rearm 0|1> <tx_bytes_per_packet> <n_re> <mod_order> <ssb_file> [flush_words=3] [bw_mode 0=high 1=low] [rate_x1024=490] [center_mhz=400]
// TX file: concatenated 2632 B packets; 3x64-bit zero flush appended per packet. Output: count * rx_bytes (lost packets = zero-filled).
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
    if (argc < 12) { std::cerr << "args\n"; return 2; }
    auto tx = slurp(argv[1]);
    size_t first = std::stoul(argv[2]), count = std::stoul(argv[3]);
    uint8_t rx_split = std::stoi(argv[4]);
    long gap_us = argc > 6 ? std::stol(argv[6]) : 0; int rearm = argc > 7 ? std::stoi(argv[7]) : 0;
    const size_t TXB = std::stoul(argv[8]), FLUSH = (argc > 12 ? std::stoul(argv[12]) : 3) * 8; int nre = std::stoi(argv[9]), mod = std::stoi(argv[10]); const char* ssbf = argv[11];
    if (first + count > tx.size()/TXB) { std::cerr << "only " << tx.size()/TXB << " packets in file\n"; return 2; }
    auto radio = helix::helix(fpga_ip);
    helix::stream_str sc{}; sc.udp_rx_mss = 1024*8; sc.udp_rx_ifg = sc.udp_rx_mss/10; sc.radio_tx_mss = pow(2,32)*8-1; sc.radio_tx_ifg = 0;
    radio.control->set_streaming_param(sc);
    radio.control->configure_radio(rx_split, SPLIT_7_3, (argc > 13 ? std::stoi(argv[13]) : BW_MODE_HIGH), nre, mod, (argc > 14 ? std::stof(argv[14]) : 490.0f)/1024, 0);
    auto rp = radio.control->get_radio_config();
    std::vector<int16_t> ssb = load_waveform_from_file(ssbf);
    radio.control->load_SSB(ssb);
    double fmhz = argc > 15 ? std::stod(argv[15]) : 400.0; radio.control->set_freq_band({{fmhz,RFDC_DAC_TYPE,0,0,true},{-fmhz,RFDC_ADC_TYPE,2,0,true}});
    uint32_t nrx = radio.control->get_num_of_rx_bytes(rx_split);
    std::cout << "SPLITCAP rx_split=" << int(rx_split) << " rx_bytes=" << nrx << " num_sch=" << rp->num_sch << " num_ptrs=" << rp->num_ptrs
              << " num_dmrs=" << rp->num_dmrs << " num_ssb=" << rp->num_ssb << " num_sc=" << rp->ofdm.num_sc << std::endl;
    helix::slot_str rx(nrx, rp->ofdm.num_sc*4);
    std::vector<uint8_t> pkt(TXB + FLUSH, 0);
    usleep(10000);
    memcpy(pkt.data(), tx.data() + first*TXB, TXB);
    radio.stream->transmit(pkt.data(), pkt.size());
    radio.control->enable_rx_radio(true);
    std::ofstream out(argv[5], std::ios::binary);
    size_t got = 0, lost = 0; std::vector<uint8_t> zero(nrx, 0);
    auto t0 = std::chrono::steady_clock::now();
    for (size_t i = first; i < first + count; i++) {
        memcpy(pkt.data(), tx.data() + i*TXB, TXB);
        rx.data.assign(nrx, 0); if (gap_us) usleep(gap_us);
        radio.stream->transmit(pkt.data(), pkt.size());
        radio.stream->receive(&rx, nrx, false, false, false);
        if (rx.data.size() != nrx) { lost++; out.write((char*)zero.data(), nrx); std::cout << "LOST " << i << std::endl;
            if (rearm) { radio.control->enable_rx_radio(false); usleep(20000); radio.control->enable_rx_radio(true); } }
        else { got++; out.write((char*)rx.data.data(), nrx); }
    }
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    radio.control->enable_rx_radio(false);
    std::cout << "SPLITCAP_RESULT rx_split=" << int(rx_split) << " packets=" << count << " received=" << got << " lost=" << lost
              << " seconds=" << dt << std::endl;
    return lost ? 1 : 0;
}
