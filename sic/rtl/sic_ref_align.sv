// sic_ref_align.sv -- TX reference tap + programmable integer-sample delay.
// dac_tdata is registered once on dac_clk and once on clk (the two clocks must be the
// same-frequency, same-phase outputs of one MMCM, as in the nlsg design; no async FIFO
// is used so the delay is identical after every reset).
// Total delay from dac_tdata to x_* is (cfg_delay + 10) samples; cfg_delay <= 2**DW - 3.
// Change cfg_delay only while SIC is disabled (the output glitches during the change).
`timescale 1ns/1ps
module sic_ref_align #(
    parameter int DW = 12                  // delay register width, in samples
)(
    input  logic               dac_clk,
    input  logic [63:0]        dac_tdata,  // {Q1, I1, Q0, I0}, sampled every dac_clk
    input  logic               clk,
    input  logic               rst_n,
    input  logic [DW-1:0]      cfg_delay,
    output logic signed [15:0] x_i0, x_q0, x_i1, x_q1
);
    localparam int AWD = DW - 1;
    logic [63:0] tap_d, tap_q;
    always_ff @(posedge dac_clk) tap_d <= dac_tdata;
    always_ff @(posedge clk)     tap_q <= tap_d;

    (* ram_style = "block" *) logic [63:0] mem [0:(1<<AWD)-1];
    logic [AWD-1:0] wptr, beats;
    logic           half;
    logic [63:0]    rd, rd_prev;
    initial begin
        for (int i = 0; i < (1<<AWD); i++) mem[i] = '0;
    end
    always_ff @(posedge clk) begin
        mem[wptr] <= tap_q;
        rd        <= mem[wptr - beats - 1'b1];
    end
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            wptr <= '0; beats <= '0; half <= 1'b0; rd_prev <= '0;
            x_i0 <= '0; x_q0 <= '0; x_i1 <= '0; x_q1 <= '0;
        end else begin
            wptr    <= wptr + 1'b1;
            beats   <= cfg_delay[DW-1:1];
            half    <= cfg_delay[0];
            rd_prev <= rd;
            if (half) begin
                x_i0 <= rd_prev[47:32]; x_q0 <= rd_prev[63:48];   // previous beat, sample 1
                x_i1 <= rd[15:0];       x_q1 <= rd[31:16];        // this beat, sample 0
            end else begin
                x_i0 <= rd[15:0];  x_q0 <= rd[31:16];
                x_i1 <= rd[47:32]; x_q1 <= rd[63:48];
            end
        end
    end
endmodule
