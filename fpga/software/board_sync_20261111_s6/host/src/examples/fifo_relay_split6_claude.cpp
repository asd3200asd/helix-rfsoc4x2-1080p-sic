// Claude 2026-11-11: FIFO <-> HELIX relay, TX SPLIT 6 (FPGA LDPC encoder, helix_nlsg_s6.bit), N_RE=73 QPSK r=490/1024.
// usage: fifo_relay_nre73_claude <tx_split73_fifo> <rx_split6_fifo> [retries=5] [nco_mhz=400]
// Reads 1352-byte units (1345-byte transport block + 7 zero bytes) from tx fifo until EOF; each is sent as is and the 1408-byte
// Split-6 result is written to rx fifo.  A unit that still fails after <retries> attempts is skipped (counted).
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include "helix_split6.h"
#include <iostream>
#include <vector>
#include <cstring>
#include <cmath>
#include <chrono>
#include <unistd.h>
#include <fcntl.h>
#include <csignal>
const char* fpga_ip = "192.168.10.2";
static bool read_exact(int fd, uint8_t* p, size_t n){ size_t g = 0; while (g < n) { ssize_t r = read(fd, p+g, n-g); if (r <= 0) return false; g += r; } return true; }
static bool write_all(int fd, const uint8_t* p, size_t n){ size_t g = 0; while (g < n) { ssize_t r = write(fd, p+g, n-g); if (r <= 0) return false; g += r; } return true; }
int main(int argc, char** argv){
    if (argc < 3) { std::cerr << "args\n"; return 2; }
    int retries = argc > 3 ? std::stoi(argv[3]) : 5; double nco = argc > 4 ? std::stod(argv[4]) : 400.0;
    const size_t TXB = 1352, RXB = 1408, FLUSH = 0;
    signal(SIGPIPE, SIG_IGN);                                 // receiver may close its FIFO first: stop cleanly instead of dying
    auto radio = helix::helix(fpga_ip);
    helix::stream_str sc{}; sc.udp_rx_mss = 1024*8; sc.udp_rx_ifg = sc.udp_rx_mss/10; sc.radio_tx_mss = pow(2,32)*8-1; sc.radio_tx_ifg = 0;
    radio.control->set_streaming_param(sc);
    radio.control->configure_radio(SPLIT_6, SPLIT_6, BW_MODE_HIGH, 73, MOD_QPSK, 490.0/1024, 0);
    auto rp = radio.control->get_radio_config();
    radio.control->load_SSB(load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));
    radio.control->set_freq_band({{nco,RFDC_DAC_TYPE,0,0,true},{-nco,RFDC_ADC_TYPE,2,0,true}});
    if (!split6::enable(radio, rp->tbs, rp->num_sch * MOD_QPSK, MOD_QPSK)) return 2;
    uint32_t nrx = radio.control->get_num_of_rx_bytes(SPLIT_6);
    if (nrx != RXB) { std::cerr << "unexpected rx bytes " << nrx << "\n"; return 2; }
    helix::slot_str rx(nrx, rp->ofdm.num_sc*4);
    std::vector<uint8_t> pkt(TXB + FLUSH, 0);
    usleep(10000);
    radio.stream->transmit(pkt.data(), pkt.size());          // warm-up slot (all-zero coded bits)
    radio.control->enable_rx_radio(true);
    std::cout << "RELAY_READY waiting for FIFOs" << std::endl;
    int rxfd = open(argv[2], O_WRONLY); int txfd = open(argv[1], O_RDONLY);
    if (rxfd < 0 || txfd < 0) { std::cerr << "fifo open failed\n"; radio.control->enable_rx_radio(false); return 2; }
    size_t units = 0, delivered = 0, skipped = 0, attempts = 0;
    auto t0 = std::chrono::steady_clock::now(); double busy = 0;
    while (read_exact(txfd, pkt.data(), TXB)) {
        units++; bool ok = false; auto a = std::chrono::steady_clock::now();
        for (int k = 0; k < retries && !ok; k++) {
            attempts++; rx.data.assign(nrx, 0);
            radio.stream->transmit(pkt.data(), pkt.size());
            radio.stream->receive(&rx, nrx, false, false, false);
            ok = rx.data.size() == nrx;
        }
        busy += std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
        if (ok) { if (!write_all(rxfd, rx.data.data(), RXB)) break; delivered++; } else skipped++;
    }
    double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    radio.control->enable_rx_radio(false); close(txfd); close(rxfd);
    std::cout << "RELAY_RESULT units=" << units << " delivered=" << delivered << " skipped=" << skipped << " attempts=" << attempts
              << " wall_seconds=" << dt << " rf_busy_seconds=" << busy << " ms_per_attempt=" << (attempts ? 1000*busy/attempts : 0) << std::endl;
    return skipped ? 1 : 0;
}
