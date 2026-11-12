// sic_top.sv -- TX reference tap/aligner + complex FIR SIC + monitor.
// ADC side packing as produced by the RFDC I and Q streams: adc_i = {I1, I0}, adc_q = {Q1, Q0}.
`timescale 1ns/1ps
module sic_top #(
    parameter int TAPS = 4,
    parameter int CW   = 18,
    parameter int CF   = 15,
    parameter int DW   = 12,
    parameter int WLOG = 16
)(
    input  logic                 adc_clk,
    input  logic                 rst_n,
    input  logic                 dac_clk,
    input  logic [63:0]          dac_tdata,
    input  logic [31:0]          adc_i,
    input  logic [31:0]          adc_q,
    output logic [31:0]          out_i,
    output logic [31:0]          out_q,
    input  logic                 cfg_we,
    input  logic [4:0]           cfg_addr,
    input  logic signed [CW-1:0] cfg_re,
    input  logic signed [CW-1:0] cfg_im,
    input  logic                 cfg_commit,
    input  logic                 cfg_enable,
    input  logic [DW-1:0]        cfg_delay,
    output logic [31:0]          cfg_epoch,
    output logic [31:0]          dly_i,       // input delayed to match out_* (monitor/capture)
    output logic [31:0]          dly_q,
    output logic [3:0]           clip,
    output logic [63:0]          pow_in,
    output logic [63:0]          pow_out,
    output logic [31:0]          clip_beats,
    output logic                 mon_toggle
);
    logic signed [15:0] x_i0, x_q0, x_i1, x_q1;
    logic signed [15:0] e_i0, e_q0, e_i1, e_q1, d_i0, d_q0, d_i1, d_q1;

    sic_ref_align #(.DW(DW)) u_align (
        .dac_clk(dac_clk), .dac_tdata(dac_tdata), .clk(adc_clk), .rst_n(rst_n),
        .cfg_delay(cfg_delay), .x_i0(x_i0), .x_q0(x_q0), .x_i1(x_i1), .x_q1(x_q1));

    sic_cfir2x #(.TAPS(TAPS), .CW(CW), .CF(CF)) u_fir (
        .clk(adc_clk), .rst_n(rst_n),
        .cfg_we(cfg_we), .cfg_addr(cfg_addr), .cfg_re(cfg_re), .cfg_im(cfg_im),
        .cfg_commit(cfg_commit), .cfg_enable(cfg_enable), .cfg_epoch(cfg_epoch),
        .y_i0(adc_i[15:0]), .y_q0(adc_q[15:0]), .y_i1(adc_i[31:16]), .y_q1(adc_q[31:16]),
        .x_i0(x_i0), .x_q0(x_q0), .x_i1(x_i1), .x_q1(x_q1),
        .e_i0(e_i0), .e_q0(e_q0), .e_i1(e_i1), .e_q1(e_q1),
        .d_i0(d_i0), .d_q0(d_q0), .d_i1(d_i1), .d_q1(d_q1), .clip(clip));

    assign out_i = {e_i1, e_i0};
    assign out_q = {e_q1, e_q0};
    assign dly_i = {d_i1, d_i0};
    assign dly_q = {d_q1, d_q0};

    sic_monitor #(.WLOG(WLOG)) u_mon (
        .clk(adc_clk), .rst_n(rst_n),
        .d_i0(d_i0), .d_q0(d_q0), .d_i1(d_i1), .d_q1(d_q1),
        .e_i0(e_i0), .e_q0(e_q0), .e_i1(e_i1), .e_q1(e_q1), .clip(clip),
        .pow_in(pow_in), .pow_out(pow_out), .clip_beats(clip_beats), .mon_toggle(mon_toggle));
endmodule
