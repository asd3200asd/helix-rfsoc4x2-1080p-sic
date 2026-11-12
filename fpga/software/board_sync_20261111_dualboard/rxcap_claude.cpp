// Claude 2026-11-12: receive-only capture at a selectable RX split (the transmitter is ANOTHER board running oneway tx).
// usage: [HELIX_FREQ_MHZ=1650] rxcap_claude <rx_split 1=6 2=7.3 3=7.2 4=7.2x 5=8> <count> <out_file>
// Output: count * rx_bytes (a slot that timed out is zero-filled).  N_RE=73 QPSK r=490/1024, same radio setup as oneway_split6_claude.
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <unistd.h>
const char* fpga_ip = "192.168.10.2";
int main(int argc, char** argv){
    if (argc < 4) { std::cerr << "args\n"; return 2; }
    uint8_t rx_split = std::stoi(argv[1]); size_t count = std::stoul(argv[2]);
    auto radio = helix::helix(fpga_ip);
    helix::stream_str sc{}; sc.udp_rx_mss = 1024*8; sc.udp_rx_ifg = sc.udp_rx_mss/10; sc.radio_tx_mss = pow(2,32)*8-1; sc.radio_tx_ifg = 0;
    radio.control->set_streaming_param(sc);
    radio.control->configure_radio(rx_split, SPLIT_7_3, BW_MODE_HIGH, 73, MOD_QPSK, 490.0/1024, 0);
    auto rp = radio.control->get_radio_config();
    radio.control->load_SSB(load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));
    const float f = getenv("HELIX_FREQ_MHZ") ? atof(getenv("HELIX_FREQ_MHZ")) : 400;
    radio.control->set_freq_band({{f,RFDC_DAC_TYPE,0,0,true},{-f,RFDC_ADC_TYPE,2,0,true}});
    uint32_t nrx = radio.control->get_num_of_rx_bytes(rx_split);
    std::cout << "RXCAP rx_split=" << int(rx_split) << " rx_bytes=" << nrx << " num_sch=" << rp->num_sch << " num_ptrs=" << rp->num_ptrs << " num_sc=" << rp->ofdm.num_sc << std::endl;
    helix::slot_str rx(nrx, rp->ofdm.num_sc*4);
    radio.control->enable_rx_radio(true);
    std::ofstream out(argv[3], std::ios::binary); size_t got = 0, lost = 0; std::vector<uint8_t> zero(nrx, 0);
    for (size_t i = 0; i < count; i++) {
        rx.data.assign(nrx, 0);
        radio.stream->receive(&rx, nrx, false, false, false);
        if (rx.data.size() != nrx) { lost++; out.write((char*)zero.data(), nrx); } else { got++; out.write((char*)rx.data.data(), nrx); }
    }
    radio.control->enable_rx_radio(false);
    std::cout << "RXCAP_RESULT rx_split=" << int(rx_split) << " received=" << got << " lost=" << lost << std::endl;
    return 0;
}
