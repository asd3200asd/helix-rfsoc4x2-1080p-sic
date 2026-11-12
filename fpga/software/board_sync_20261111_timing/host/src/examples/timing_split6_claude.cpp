// Claude 2026-11-11: per-packet timeline of the TX split-6 path (helix_nlsg_s6.bit), N_RE=73 QPSK r=490/1024.
// usage: timing_split6_claude <tb_file 1345B each> <expected_split6 1408B each> <count> <mode> <rx_split> <out.csv> [gap_us=2000]
//   mode 0: triggerTX, data, triggerRX, receive (same order as the soak / original loopback)
//   mode 1: triggerRX first (RX armed), then triggerTX, data, receive  -> RX data leaves the FPGA as soon as it is decoded
//   rx_split 1 = Split 6 (1408 B, checked against expected), 5 = Split 8 raw IQ (123792 B, not checked)
// CSV (CLOCK_MONOTONIC ns): i,t_start,t_rxtrig_ack,t_txtrig_ack,t_data_sent,t_rx_done,ok   (t_rxtrig_ack sits where it happened)
#include "../include/helix.h"
#include "../include/defines.h"
#include "helpers.h"
#include "helix_split6.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <cstring>
#include <cmath>
#include <ctime>
#include <unistd.h>
const char* fpga_ip = "192.168.10.2";
static uint64_t now() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static std::vector<uint8_t> slurp(const char* fn){ std::ifstream f(fn, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static bool trig(helix::helix& r, const char* name, std::vector<std::string> args) { helix::cmd_struct c; c.cmd = name; c.cmdArgs = args; return r.cmd->sendCmd(c, true) == UDP_CMD_ACK; }
int main(int argc, char** argv){
    if (argc < 7) { std::cerr << "args\n"; return 2; }
    auto tb = slurp(argv[1]); auto exp = slurp(argv[2]); size_t count = std::stoul(argv[3]); int mode = std::stoi(argv[4]); uint8_t rx_split = std::stoi(argv[5]);
    long gap_us = argc > 7 ? std::stol(argv[7]) : 2000; const size_t TBB = 1345, TXB = 1352, RX6 = 1408;
    if (count > tb.size() / TBB) count = tb.size() / TBB;
    auto radio = helix::helix(fpga_ip);
    helix::stream_str sc{}; sc.udp_rx_mss = 1024*8; sc.udp_rx_ifg = sc.udp_rx_mss/10; sc.radio_tx_mss = pow(2,32)*8-1; sc.radio_tx_ifg = 0;
    radio.control->set_streaming_param(sc);
    radio.control->configure_radio(rx_split, SPLIT_6, BW_MODE_HIGH, 73, MOD_QPSK, 490.0/1024, 0);
    auto rp = radio.control->get_radio_config();
    radio.control->load_SSB(load_waveform_from_file("matlab/TX_data/QPSK/N_RE_73_RATE_490/slotFR2_CH1_SSB_TX1.txt"));
    radio.control->set_freq_band({{400,RFDC_DAC_TYPE,0,0,true},{-400,RFDC_ADC_TYPE,2,0,true}});
    if (!split6::enable(radio, rp->tbs, rp->num_sch * MOD_QPSK, MOD_QPSK)) return 2;
    const uint32_t nrx = radio.control->get_num_of_rx_bytes(rx_split);
    std::vector<uint8_t> pkt(TXB, 0), rx(nrx, 0);
    const std::string s_tx = std::to_string(TXB), s_rx = std::to_string(nrx);
    usleep(10000);
    memcpy(pkt.data(), tb.data(), TBB);
    if (!trig(radio, "triggerTX ", {s_tx})) return 3;
    radio.udp->data_socket.send(pkt.data(), TXB);            // warm-up slot
    radio.control->enable_rx_radio(true);
    std::ofstream csv(argv[6]); size_t ok = 0, got = 0;
    for (size_t i = 0; i < count; i++) {
        memcpy(pkt.data(), tb.data() + i*TBB, TBB);
        uint64_t t0 = now(), ta = 0, tb_ = 0, tc = 0, td = 0;
        if (mode == 1) { trig(radio, "triggerRX ", {s_rx, "0", "0", "0", "1"}); ta = now(); }
        trig(radio, "triggerTX ", {s_tx}); tb_ = now();
        radio.udp->data_socket.send(pkt.data(), TXB); tc = now();
        if (mode == 0) { trig(radio, "triggerRX ", {s_rx, "0", "0", "0", "1"}); ta = now(); }
        ssize_t n = radio.udp->data_socket.recv(rx.data(), nrx); td = now();
        bool good = (n == (ssize_t)nrx) && (rx_split != SPLIT_6 || !memcmp(rx.data(), exp.data() + i*RX6, RX6));
        got += (n == (ssize_t)nrx); ok += good;
        csv << i << "," << t0 << "," << ta << "," << tb_ << "," << tc << "," << td << "," << good << "\n";
        if (n != (ssize_t)nrx) usleep(300000);                  // let a late packet pass before the next one
        usleep(gap_us);
    }
    radio.control->enable_rx_radio(false);
    std::cout << "TIMING_RESULT mode=" << mode << " rx_split=" << (int)rx_split << " packets=" << count << " received=" << got << " ok=" << ok << " encoder_done=" << split6::packets_done(radio) << std::endl;
    return 0;
}
