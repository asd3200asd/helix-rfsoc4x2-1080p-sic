// Claude 2026-11-12: receive-only, one slot per trigger (like the original throughput_experiment_rx), RX split 6, with the FPGA's
// per-slot metadata (energy + CFO estimate).  Prints one line per slot: ok(CRC of the oneway TB) cfo snr signal noise.
// usage: [HELIX_FREQ_MHZ=1650] rxmeta_claude <count> > log     (transmitter: another board running oneway tx)
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <unistd.h>
const char* fpga_ip = "192.168.10.2";
static uint32_t crc_tab[256];
static uint32_t crc32(const uint8_t* p, size_t n) { uint32_t c = 0xFFFFFFFFu; while (n--) c = crc_tab[(c ^ *p++) & 0xFF] ^ (c >> 8); return ~c; }
int main(int argc, char** argv){
    for (uint32_t i = 0; i < 256; i++) { uint32_t c = i; for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; crc_tab[i] = c; }
    size_t count = argc > 1 ? std::stoul(argv[1]) : 1000;
    auto radio = helix::helix(fpga_ip);
    helix::stream_str sc{}; sc.udp_rx_mss = 1408; sc.udp_rx_ifg = 128; sc.radio_tx_mss = 1352; sc.radio_tx_ifg = 180000;
    radio.control->set_streaming_param(sc);
    radio.control->configure_radio(SPLIT_6, SPLIT_6, BW_MODE_HIGH, 73, MOD_QPSK, 490.0/1024, 0);
    auto rp = radio.control->get_radio_config();
    radio.control->load_SSB(load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));
    const float f = getenv("HELIX_FREQ_MHZ") ? atof(getenv("HELIX_FREQ_MHZ")) : 400;
    radio.control->set_freq_band({{f,RFDC_DAC_TYPE,0,0,true},{-f,RFDC_ADC_TYPE,2,0,true}});
    const size_t RXB = 1408, TBB = 1345, HDR = 8, PAY = TBB - HDR;
    helix::slot_str rx(RXB, rp->ofdm.num_sc*4);
    radio.control->enable_rx_radio(true);
    size_t got = 0, good = 0; std::vector<uint8_t> tb(TBB);
    for (size_t i = 0; i < count; i++) {
        rx.data.assign(RXB, 0); rx.cfo = 0; rx.snr = 0;
        radio.stream->receive(&rx, RXB, false, true, true);
        if (rx.data.size() != RXB) { printf("L\n"); continue; }
        got++; memcpy(tb.data(), rx.data.data(), 674); memcpy(tb.data() + 674, rx.data.data() + 704, 671);
        size_t len = tb[0] | (tb[1] << 8); uint32_t c; memcpy(&c, tb.data() + 4, 4);
        bool ok = len && len <= PAY && crc32(tb.data() + HDR, len) == c; good += ok;
        printf("%d %.6g %.6g %.6g %.6g\n", ok ? 1 : 0, rx.cfo, rx.snr, rx.signal_pow, rx.noise_pow);
    }
    radio.control->enable_rx_radio(false);
    fprintf(stderr, "RXMETA_RESULT slots=%zu got=%zu good=%zu\n", count, got, good);
    return 0;
}
