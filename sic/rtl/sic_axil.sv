// sic_axil.sv -- AXI4-Lite register front end around sic_ref_align + sic_cfir2x + sic_monitor,
// plus a paired capture buffer (reference/RX or delayed-RX/SIC-output) for calibration.
// Everything runs on aclk (= adc_clk). After reset the SIC is disabled (bit-exact bypass).
//
// Register map (32-bit, offsets):
//  0x00 ID        RO  0x51C00001
//  0x04 INFO      RO  {CF[7:0], CW[7:0], DW[7:0], TAPS[7:0]}
//  0x08 CTRL      RW  bit0 = SIC enable
//  0x0C COMMIT    WO  any write: shadow coefficients -> active (one clock)
//  0x10 DELAY     RW  reference delay in samples (total = DELAY + 10)
//  0x14 EPOCH     RO  number of commits
//  0x18 STATUS    RO  bit0 monitor toggle, bit1 capture busy, bit2 capture done
//  0x20/0x24      RO  power before SIC (lo/hi), last monitor window of 2**WLOG beats
//  0x28/0x2C      RO  power after SIC (lo/hi)
//  0x30 CLIPS     RO  beats with saturation in the last window
//  0x40 CAP_CTRL  RW  bit0 = start (self-clearing), bit1 = source (0: A=reference,B=RX in; 1: A=RX delayed,B=SIC out)
//  0x44 CAP_THR   RW  start when |ref.i0|+|ref.q0| > THR (0 = start immediately)
//  0x48 CAP_ADDR  RW  entry index for read-back
//  0x50..0x5C     RO  entry: A = {0x54,0x50} = {Q1,I1,Q0,I0}, B = {0x5C,0x58} = {Q1,I1,Q0,I0}
//  0x100+8k       RW  shadow coefficient k real part (sign-extended)
//  0x104+8k       RW  shadow coefficient k imaginary part; this write pushes tap k into the core
`timescale 1ns/1ps
module sic_axil #(
    parameter int TAPS   = 4,
    parameter int CW     = 18,
    parameter int CF     = 15,
    parameter int DW     = 12,
    parameter int WLOG   = 22,
    parameter int CAP_AW = 12
)(
    input  logic        aclk,
    input  logic        aresetn,
    input  logic [11:0] s_axi_awaddr,
    input  logic        s_axi_awvalid,
    output logic        s_axi_awready,
    input  logic [31:0] s_axi_wdata,
    input  logic [3:0]  s_axi_wstrb,
    input  logic        s_axi_wvalid,
    output logic        s_axi_wready,
    output logic [1:0]  s_axi_bresp,
    output logic        s_axi_bvalid,
    input  logic        s_axi_bready,
    input  logic [11:0] s_axi_araddr,
    input  logic        s_axi_arvalid,
    output logic        s_axi_arready,
    output logic [31:0] s_axi_rdata,
    output logic [1:0]  s_axi_rresp,
    output logic        s_axi_rvalid,
    input  logic        s_axi_rready,
    input  logic        dac_clk,
    input  logic [63:0] dac_tdata,
    input  logic [31:0] adc_i,
    input  logic [31:0] adc_q,
    input  logic        adc_valid,
    output logic [31:0] out_i,
    output logic [31:0] out_q,
    output logic        out_valid
);
    localparam int LEVELS = (TAPS > 1) ? $clog2(TAPS) : 0;
    localparam int LAT    = LEVELS + 5;

    // ---------------- core configuration signals ----------------
    logic                 cfg_we, cfg_commit, cfg_enable;
    logic [4:0]           cfg_addr;
    logic signed [CW-1:0] cfg_re, cfg_im;
    logic [DW-1:0]        cfg_delay;
    logic [31:0]          cfg_epoch;
    logic signed [15:0]   x_i0, x_q0, x_i1, x_q1;
    logic signed [15:0]   e_i0, e_q0, e_i1, e_q1, d_i0, d_q0, d_i1, d_q1;
    logic [3:0]           clip;
    logic [63:0]          pow_in, pow_out;
    logic [31:0]          clip_beats;
    logic                 mon_toggle;

    sic_ref_align #(.DW(DW)) u_align (
        .dac_clk(dac_clk), .dac_tdata(dac_tdata), .clk(aclk), .rst_n(aresetn),
        .cfg_delay(cfg_delay), .x_i0(x_i0), .x_q0(x_q0), .x_i1(x_i1), .x_q1(x_q1));
    sic_cfir2x #(.TAPS(TAPS), .CW(CW), .CF(CF)) u_fir (
        .clk(aclk), .rst_n(aresetn),
        .cfg_we(cfg_we), .cfg_addr(cfg_addr), .cfg_re(cfg_re), .cfg_im(cfg_im),
        .cfg_commit(cfg_commit), .cfg_enable(cfg_enable), .cfg_epoch(cfg_epoch),
        .y_i0(adc_i[15:0]), .y_q0(adc_q[15:0]), .y_i1(adc_i[31:16]), .y_q1(adc_q[31:16]),
        .x_i0(x_i0), .x_q0(x_q0), .x_i1(x_i1), .x_q1(x_q1),
        .e_i0(e_i0), .e_q0(e_q0), .e_i1(e_i1), .e_q1(e_q1),
        .d_i0(d_i0), .d_q0(d_q0), .d_i1(d_i1), .d_q1(d_q1), .clip(clip));
    sic_monitor #(.WLOG(WLOG)) u_mon (
        .clk(aclk), .rst_n(aresetn),
        .d_i0(d_i0), .d_q0(d_q0), .d_i1(d_i1), .d_q1(d_q1),
        .e_i0(e_i0), .e_q0(e_q0), .e_i1(e_i1), .e_q1(e_q1), .clip(clip),
        .pow_in(pow_in), .pow_out(pow_out), .clip_beats(clip_beats), .mon_toggle(mon_toggle));

    assign out_i = {e_i1, e_i0};
    assign out_q = {e_q1, e_q0};
    logic [LAT-1:0] vpipe;
    always_ff @(posedge aclk) begin
        if (!aresetn) vpipe <= '0;
        else          vpipe <= {vpipe[LAT-2:0], adc_valid};
    end
    assign out_valid = vpipe[LAT-1];

    // ---------------- capture ----------------
    logic                cap_start, cap_sel, cap_busy, cap_done, cap_run, cap_wr;
    logic [15:0]         cap_thr;
    logic [CAP_AW-1:0]   cap_waddr, cap_raddr;
    logic [127:0]        cap_din, cap_dout;
    logic [16:0]         x_mag;
    (* ram_style = "block" *) logic [127:0] cmem [0:(1<<CAP_AW)-1];
    always_comb begin
        x_mag = (x_i0[15] ? 17'(-17'(x_i0)) : 17'(x_i0)) + (x_q0[15] ? 17'(-17'(x_q0)) : 17'(x_q0));
        if (cap_sel) cap_din = {e_q1, e_i1, e_q0, e_i0, d_q1, d_i1, d_q0, d_i0};
        else         cap_din = {adc_q[31:16], adc_i[31:16], adc_q[15:0], adc_i[15:0], x_q1, x_i1, x_q0, x_i0};
        cap_wr = cap_run | (cap_busy & ~cap_run & ((cap_thr == 16'd0) | (x_mag > {1'b0, cap_thr})));
    end
    always_ff @(posedge aclk) begin
        if (cap_wr) cmem[cap_waddr] <= cap_din;
        cap_dout <= cmem[cap_raddr];
    end
    always_ff @(posedge aclk) begin
        if (!aresetn) begin
            cap_busy <= 1'b0; cap_done <= 1'b0; cap_run <= 1'b0; cap_waddr <= '0;
        end else begin
            if (cap_start) begin
                cap_busy <= 1'b1; cap_done <= 1'b0; cap_run <= 1'b0; cap_waddr <= '0;
            end else if (cap_wr) begin
                cap_run   <= 1'b1;
                cap_waddr <= cap_waddr + 1'b1;
                if (&cap_waddr) begin cap_run <= 1'b0; cap_busy <= 1'b0; cap_done <= 1'b1; end
            end
        end
    end

    // ---------------- AXI4-Lite ----------------
    logic signed [CW-1:0] sh_re [0:TAPS-1], sh_im [0:TAPS-1];
    logic        wr_en, rd_s1, rd_s2;
    logic [11:0] wr_addr, rd_addr;
    logic [31:0] wr_data;
    wire  [4:0]  wr_tap = wr_addr[7:3];
    wire  [4:0]  rd_tap = rd_addr[7:3];
    assign s_axi_bresp = 2'b00;
    assign s_axi_rresp = 2'b00;

    always_ff @(posedge aclk) begin
        if (!aresetn) begin
            s_axi_awready <= 1'b0; s_axi_wready <= 1'b0; s_axi_bvalid <= 1'b0; wr_en <= 1'b0;
            wr_addr <= '0; wr_data <= '0;
            cfg_enable <= 1'b0; cfg_delay <= '0; cfg_commit <= 1'b0; cfg_we <= 1'b0;
            cfg_addr <= '0; cfg_re <= '0; cfg_im <= '0;
            cap_start <= 1'b0; cap_sel <= 1'b0; cap_thr <= '0; cap_raddr <= '0;
            for (int k = 0; k < TAPS; k++) begin sh_re[k] <= '0; sh_im[k] <= '0; end
        end else begin
            s_axi_awready <= 1'b0; s_axi_wready <= 1'b0; wr_en <= 1'b0;
            cfg_commit <= 1'b0; cfg_we <= 1'b0; cap_start <= 1'b0;
            if (s_axi_awvalid && s_axi_wvalid && !s_axi_awready && !wr_en && !s_axi_bvalid) begin
                s_axi_awready <= 1'b1; s_axi_wready <= 1'b1; wr_en <= 1'b1;
                wr_addr <= s_axi_awaddr; wr_data <= s_axi_wdata;
            end
            if (wr_en) begin
                s_axi_bvalid <= 1'b1;
                if (wr_addr[11:8] == 4'h0) begin
                    case (wr_addr[7:2])
                        6'h02: cfg_enable <= wr_data[0];
                        6'h03: cfg_commit <= 1'b1;
                        6'h04: cfg_delay  <= wr_data[DW-1:0];
                        6'h10: begin cap_start <= wr_data[0]; cap_sel <= wr_data[1]; end
                        6'h11: cap_thr   <= wr_data[15:0];
                        6'h12: cap_raddr <= wr_data[CAP_AW-1:0];
                        default: ;
                    endcase
                end else if (wr_addr[11:8] == 4'h1 && wr_tap < TAPS) begin
                    if (!wr_addr[2]) sh_re[wr_tap] <= wr_data[CW-1:0];
                    else begin
                        sh_im[wr_tap] <= wr_data[CW-1:0];
                        cfg_we <= 1'b1; cfg_addr <= wr_tap;
                        cfg_re <= sh_re[wr_tap]; cfg_im <= wr_data[CW-1:0];
                    end
                end
            end else if (s_axi_bvalid && s_axi_bready) s_axi_bvalid <= 1'b0;
        end
    end

    always_ff @(posedge aclk) begin
        if (!aresetn) begin
            s_axi_arready <= 1'b0; s_axi_rvalid <= 1'b0; s_axi_rdata <= '0;
            rd_s1 <= 1'b0; rd_s2 <= 1'b0; rd_addr <= '0;
        end else begin
            s_axi_arready <= 1'b0; rd_s1 <= 1'b0; rd_s2 <= rd_s1;
            if (s_axi_arvalid && !s_axi_arready && !rd_s1 && !rd_s2 && !s_axi_rvalid) begin
                s_axi_arready <= 1'b1; rd_s1 <= 1'b1; rd_addr <= s_axi_araddr;
            end
            if (rd_s2) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rdata  <= 32'd0;
                if (rd_addr[11:8] == 4'h0) begin
                    case (rd_addr[7:2])
                        6'h00: s_axi_rdata <= 32'h51C0_0001;
                        6'h01: s_axi_rdata <= {8'(CF), 8'(CW), 8'(DW), 8'(TAPS)};
                        6'h02: s_axi_rdata <= {31'd0, cfg_enable};
                        6'h04: s_axi_rdata <= 32'(cfg_delay);
                        6'h05: s_axi_rdata <= cfg_epoch;
                        6'h06: s_axi_rdata <= {29'd0, cap_done, cap_busy, mon_toggle};
                        6'h08: s_axi_rdata <= pow_in[31:0];
                        6'h09: s_axi_rdata <= pow_in[63:32];
                        6'h0A: s_axi_rdata <= pow_out[31:0];
                        6'h0B: s_axi_rdata <= pow_out[63:32];
                        6'h0C: s_axi_rdata <= clip_beats;
                        6'h10: s_axi_rdata <= {30'd0, cap_sel, 1'b0};
                        6'h11: s_axi_rdata <= {16'd0, cap_thr};
                        6'h12: s_axi_rdata <= 32'(cap_raddr);
                        6'h14: s_axi_rdata <= cap_dout[31:0];
                        6'h15: s_axi_rdata <= cap_dout[63:32];
                        6'h16: s_axi_rdata <= cap_dout[95:64];
                        6'h17: s_axi_rdata <= cap_dout[127:96];
                        default: ;
                    endcase
                end else if (rd_addr[11:8] == 4'h1 && rd_tap < TAPS) begin
                    s_axi_rdata <= rd_addr[2] ? 32'(sh_im[rd_tap]) : 32'(sh_re[rd_tap]);
                end
            end else if (s_axi_rvalid && s_axi_rready) s_axi_rvalid <= 1'b0;
        end
    end
endmodule
