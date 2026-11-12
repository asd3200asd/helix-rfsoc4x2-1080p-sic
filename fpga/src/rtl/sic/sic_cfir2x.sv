// sic_cfir2x.sv -- minimal post-DDC complex-FIR self-interference canceller.
// Clean rewrite (2026-11-11); shares no code with the v107 SIC sources.
//
// * Free-running sample pipeline: every clock carries 2 complex samples
//   (sample 0 is earlier in time). No handshake, fixed latency LAT clocks.
// * e[n] = y[n] - round( sum_k h[k] * x[n-k] ),  h signed CW bits, CF fractional.
// * cfg_enable = 0 -> output is the input delayed by LAT (bit-exact bypass).
// * Coefficients: write shadow bank, then cfg_commit swaps all taps in one clock.
//   A commit in clock c is used from the beat entering in clock c onward.
//   Do not assert cfg_we and cfg_commit in the same clock.
`timescale 1ns/1ps
module sic_cfir2x #(
    parameter int TAPS = 4,     // 1..16
    parameter int CW   = 18,    // coefficient width (signed)
    parameter int CF   = 15     // coefficient fractional bits
)(
    input  logic                 clk,
    input  logic                 rst_n,          // synchronous, active low
    input  logic                 cfg_we,
    input  logic [4:0]           cfg_addr,
    input  logic signed [CW-1:0] cfg_re,
    input  logic signed [CW-1:0] cfg_im,
    input  logic                 cfg_commit,
    input  logic                 cfg_enable,
    output logic [31:0]          cfg_epoch,
    input  logic signed [15:0]   y_i0, y_q0, y_i1, y_q1,   // received samples
    input  logic signed [15:0]   x_i0, x_q0, x_i1, x_q1,   // aligned TX reference
    output logic signed [15:0]   e_i0, e_q0, e_i1, e_q1,   // SIC output
    output logic signed [15:0]   d_i0, d_q0, d_i1, d_q1,   // input delayed by LAT (monitor)
    output logic [3:0]           clip                      // {q1,i1,q0,i0} saturated this beat
);
    localparam int LEVELS = (TAPS > 1) ? $clog2(TAPS) : 0;
    localparam int PAD    = 1 << LEVELS;
    localparam int PW     = 16 + CW;        // one real product
    localparam int TW     = PW + 1;         // one complex tap term
    localparam int AW     = TW + LEVELS;    // full sum
    localparam int YD     = LEVELS + 4;     // y delay before the subtract stage
    // total latency in clocks: LEVELS + 5

    // ---------------- coefficient banks ----------------
    logic signed [CW-1:0] sh_re [0:TAPS-1], sh_im [0:TAPS-1];
    logic signed [CW-1:0] ac_re [0:TAPS-1], ac_im [0:TAPS-1];
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int k = 0; k < TAPS; k++) begin
                sh_re[k] <= '0; sh_im[k] <= '0; ac_re[k] <= '0; ac_im[k] <= '0;
            end
            cfg_epoch <= '0;
        end else begin
            if (cfg_we && (cfg_addr < TAPS)) begin
                sh_re[cfg_addr] <= cfg_re; sh_im[cfg_addr] <= cfg_im;
            end
            if (cfg_commit) begin
                for (int k = 0; k < TAPS; k++) begin
                    ac_re[k] <= sh_re[k]; ac_im[k] <= sh_im[k];
                end
                cfg_epoch <= cfg_epoch + 32'd1;
            end
        end
    end

    // ---------------- stage 0: reference window, y and enable capture ----------------
    // h?[0] newest (x1 of this beat), h?[1] = x0 of this beat, h?[2] = x1 of previous beat ...
    logic signed [15:0] hr [0:TAPS], hi [0:TAPS];
    logic signed [15:0] ypi0 [0:YD-1], ypq0 [0:YD-1], ypi1 [0:YD-1], ypq1 [0:YD-1];
    logic               enp  [0:YD-1];
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            for (int j = 0; j <= TAPS; j++) begin hr[j] <= '0; hi[j] <= '0; end
            for (int j = 0; j < YD; j++) begin
                ypi0[j] <= '0; ypq0[j] <= '0; ypi1[j] <= '0; ypq1[j] <= '0; enp[j] <= 1'b0;
            end
        end else begin
            hr[0] <= x_i1; hi[0] <= x_q1;
            hr[1] <= x_i0; hi[1] <= x_q0;
            for (int j = 2; j <= TAPS; j++) begin hr[j] <= hr[j-2]; hi[j] <= hi[j-2]; end
            ypi0[0] <= y_i0; ypq0[0] <= y_q0; ypi1[0] <= y_i1; ypq1[0] <= y_q1;
            enp[0]  <= cfg_enable;
            for (int j = 1; j < YD; j++) begin
                ypi0[j] <= ypi0[j-1]; ypq0[j] <= ypq0[j-1];
                ypi1[j] <= ypi1[j-1]; ypq1[j] <= ypq1[j-1];
                enp[j]  <= enp[j-1];
            end
        end
    end

    // ---------------- stage 1: real products (lane 0 = sample 0, lane 1 = sample 1) ----------------
    logic signed [PW-1:0] prr [0:1][0:TAPS-1], pii [0:1][0:TAPS-1];
    logic signed [PW-1:0] pri [0:1][0:TAPS-1], pir [0:1][0:TAPS-1];
    always_ff @(posedge clk) begin
        for (int l = 0; l < 2; l++)
            for (int k = 0; k < TAPS; k++) begin
                prr[l][k] <= ac_re[k] * hr[k + 1 - l];
                pii[l][k] <= ac_im[k] * hi[k + 1 - l];
                pri[l][k] <= ac_re[k] * hi[k + 1 - l];
                pir[l][k] <= ac_im[k] * hr[k + 1 - l];
            end
    end

    // ---------------- stage 2 + adder tree ----------------
    logic signed [AW-1:0] sr [0:LEVELS][0:1][0:PAD-1];
    logic signed [AW-1:0] si [0:LEVELS][0:1][0:PAD-1];
    always_ff @(posedge clk) begin
        for (int l = 0; l < 2; l++)
            for (int k = 0; k < PAD; k++) begin
                if (k < TAPS) begin
                    sr[0][l][k] <= AW'(prr[l][k]) - AW'(pii[l][k]);
                    si[0][l][k] <= AW'(pri[l][k]) + AW'(pir[l][k]);
                end else begin
                    sr[0][l][k] <= '0; si[0][l][k] <= '0;
                end
            end
        for (int v = 1; v <= LEVELS; v++)
            for (int l = 0; l < 2; l++)
                for (int k = 0; k < (PAD >> v); k++) begin
                    sr[v][l][k] <= sr[v-1][l][2*k] + sr[v-1][l][2*k+1];
                    si[v][l][k] <= si[v-1][l][2*k] + si[v-1][l][2*k+1];
                end
    end

    // ---------------- round, saturate estimate ----------------
    function automatic logic signed [16:0] rnd_sat(input logic signed [AW-1:0] a);
        logic signed [AW:0] t;
        logic signed [AW:0] s;
        begin
            t = (AW+1)'(a) + ((AW+1)'(1) <<< (CF-1));
            s = t >>> CF;
            if (s > 32767)       rnd_sat = {1'b1, 16'sd32767};
            else if (s < -32768) rnd_sat = {1'b1, 16'sh8000};
            else                 rnd_sat = {1'b0, s[15:0]};
        end
    endfunction
    function automatic logic signed [16:0] sub_sat(input logic signed [15:0] a, input logic signed [15:0] b);
        logic signed [16:0] dlt;
        begin
            dlt = 17'(a) - 17'(b);
            if (dlt > 32767)       sub_sat = {1'b1, 16'sd32767};
            else if (dlt < -32768) sub_sat = {1'b1, 16'sh8000};
            else                   sub_sat = {1'b0, dlt[15:0]};
        end
    endfunction

    logic signed [15:0] est_r [0:1], est_i [0:1];
    logic               est_cr [0:1], est_ci [0:1];
    always_ff @(posedge clk) begin
        for (int l = 0; l < 2; l++) begin
            {est_cr[l], est_r[l]} <= rnd_sat(sr[LEVELS][l][0]);
            {est_ci[l], est_i[l]} <= rnd_sat(si[LEVELS][l][0]);
        end
    end

    // ---------------- subtract ----------------
    logic signed [16:0] o_i0, o_q0, o_i1, o_q1;
    always_comb begin
        o_i0 = sub_sat(ypi0[YD-1], est_r[0]);
        o_q0 = sub_sat(ypq0[YD-1], est_i[0]);
        o_i1 = sub_sat(ypi1[YD-1], est_r[1]);
        o_q1 = sub_sat(ypq1[YD-1], est_i[1]);
    end
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            e_i0 <= '0; e_q0 <= '0; e_i1 <= '0; e_q1 <= '0;
            d_i0 <= '0; d_q0 <= '0; d_i1 <= '0; d_q1 <= '0; clip <= '0;
        end else begin
            d_i0 <= ypi0[YD-1]; d_q0 <= ypq0[YD-1]; d_i1 <= ypi1[YD-1]; d_q1 <= ypq1[YD-1];
            if (enp[YD-1]) begin
                e_i0 <= o_i0[15:0]; e_q0 <= o_q0[15:0]; e_i1 <= o_i1[15:0]; e_q1 <= o_q1[15:0];
                clip <= {o_q1[16] | est_ci[1], o_i1[16] | est_cr[1],
                         o_q0[16] | est_ci[0], o_i0[16] | est_cr[0]};
            end else begin
                e_i0 <= ypi0[YD-1]; e_q0 <= ypq0[YD-1]; e_i1 <= ypi1[YD-1]; e_q1 <= ypq1[YD-1];
                clip <= '0;
            end
        end
    end
endmodule
