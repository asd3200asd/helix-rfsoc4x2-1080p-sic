// sic_monitor.sv -- power before/after SIC over windows of 2**WLOG beats, plus clip count.
// Values are latched together; mon_toggle flips at every latch.
`timescale 1ns/1ps
module sic_monitor #(
    parameter int WLOG = 16
)(
    input  logic               clk,
    input  logic               rst_n,
    input  logic signed [15:0] d_i0, d_q0, d_i1, d_q1,
    input  logic signed [15:0] e_i0, e_q0, e_i1, e_q1,
    input  logic [3:0]         clip,
    output logic [63:0]        pow_in,
    output logic [63:0]        pow_out,
    output logic [31:0]        clip_beats,
    output logic               mon_toggle
);
    function automatic logic [33:0] sq16(input logic signed [15:0] a);
        logic signed [31:0] p;
        begin
            p = a * a;
            sq16 = {2'b00, p};
        end
    endfunction
    logic [33:0] sq_d, sq_e;
    logic        cl;
    logic [63:0] acc_d, acc_e;
    logic [31:0] acc_c;
    logic [WLOG-1:0] cnt;
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            sq_d <= '0; sq_e <= '0; cl <= 1'b0; acc_d <= '0; acc_e <= '0; acc_c <= '0;
            cnt <= '0; pow_in <= '0; pow_out <= '0; clip_beats <= '0; mon_toggle <= 1'b0;
        end else begin
            sq_d <= sq16(d_i0) + sq16(d_q0) + sq16(d_i1) + sq16(d_q1);
            sq_e <= sq16(e_i0) + sq16(e_q0) + sq16(e_i1) + sq16(e_q1);
            cl   <= |clip;
            cnt  <= cnt + 1'b1;
            if (&cnt) begin
                pow_in     <= acc_d + sq_d;
                pow_out    <= acc_e + sq_e;
                clip_beats <= acc_c + cl;
                mon_toggle <= ~mon_toggle;
                acc_d <= '0; acc_e <= '0; acc_c <= '0;
            end else begin
                acc_d <= acc_d + sq_d; acc_e <= acc_e + sq_e; acc_c <= acc_c + cl;
            end
        end
    end
endmodule
