// Self-checking test of the AXI-Lite front end: the data path must match a sic_top instance
// (already verified against the Python model) driven by the same configuration strobes,
// and registers, capture and monitor read-back must return what the hardware holds.
`timescale 1ns/1ps
module tb_sic_axil;
    localparam int TAPS = 4, CAP_AW = 8, WLOG = 8, NCAP = 1 << CAP_AW;
    logic clk = 0; always #2.034 clk = ~clk;
    logic rstn = 0;
    logic [11:0] awaddr = 0, araddr = 0; logic awvalid = 0, wvalid = 0, bready = 0, arvalid = 0, rready = 0;
    logic [31:0] wdata = 0, rdata; logic awready, wready, bvalid, arready, rvalid; logic [1:0] bresp, rresp;
    logic [63:0] dac = 0; logic [31:0] ai = 0, aq = 0; logic av = 0;
    wire [31:0] oi, oq; wire ov, s_ir, s_qr, m_iv, m_qv;
    int errors = 0, compared = 0;

    sic_axil_bd #(.TAPS(TAPS), .CAP_AW(CAP_AW), .WLOG(WLOG)) dut (
        .aclk(clk), .aresetn(rstn),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(bresp), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(rresp), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .s_axis_i_tdata(ai), .s_axis_i_tvalid(av), .s_axis_i_tready(s_ir),
        .s_axis_q_tdata(aq), .s_axis_q_tvalid(av), .s_axis_q_tready(s_qr),
        .m_axis_i_tdata(oi), .m_axis_i_tvalid(m_iv), .m_axis_i_tready(1'b1),
        .m_axis_q_tdata(oq), .m_axis_q_tvalid(m_qv), .m_axis_q_tready(1'b1),
        .dac_clk(clk), .dac_tdata(dac));

    wire [31:0] ri, rq, rdi, rdq, repoch, rclips; wire [3:0] rclip; wire [63:0] rpin, rpout; wire rtog;
    sic_top #(.TAPS(TAPS), .WLOG(WLOG)) refm (
        .adc_clk(clk), .rst_n(rstn), .dac_clk(clk), .dac_tdata(dac), .adc_i(ai), .adc_q(aq),
        .out_i(ri), .out_q(rq),
        .cfg_we(dut.u_sic.cfg_we), .cfg_addr(dut.u_sic.cfg_addr), .cfg_re(dut.u_sic.cfg_re), .cfg_im(dut.u_sic.cfg_im),
        .cfg_commit(dut.u_sic.cfg_commit), .cfg_enable(dut.u_sic.cfg_enable), .cfg_delay(dut.u_sic.cfg_delay),
        .cfg_epoch(repoch), .dly_i(rdi), .dly_q(rdq), .clip(rclip),
        .pow_in(rpin), .pow_out(rpout), .clip_beats(rclips), .mon_toggle(rtog));

    // random sample streams, never stopping
    always @(posedge clk) begin
        ai <= $urandom; aq <= $urandom; dac <= {$urandom, $urandom}; av <= rstn;
    end
    // data path must equal the reference instance on every clock
    always @(negedge clk) if (rstn) begin
        compared++;
        if (oi !== ri || oq !== rq) begin errors++; if (errors < 10) $display("DATA MISMATCH t=%0t", $time); end
        if (m_iv !== m_qv) errors++;
    end
    // expected capture contents, built from the reference instance and the TB inputs
    logic [127:0] expq [$];
    always @(posedge clk) if (rstn && dut.u_sic.cap_wr) begin
        if (dut.u_sic.cap_sel) expq.push_back({rq[31:16], ri[31:16], rq[15:0], ri[15:0], rdq[31:16], rdi[31:16], rdq[15:0], rdi[15:0]});
        else expq.push_back({aq[31:16], ai[31:16], aq[15:0], ai[15:0],
                             refm.x_q1, refm.x_i1, refm.x_q0, refm.x_i0});
    end

    task automatic wr(input [11:0] a, input [31:0] d);
        @(posedge clk); awaddr <= a; wdata <= d; awvalid <= 1; wvalid <= 1; bready <= 1;
        do @(posedge clk); while (!(awready && wready));
        awvalid <= 0; wvalid <= 0;
        do @(posedge clk); while (!bvalid);
        bready <= 0;
    endtask
    task automatic rd(input [11:0] a, output [31:0] d);
        @(posedge clk); araddr <= a; arvalid <= 1; rready <= 1;
        do @(posedge clk); while (!arready);
        arvalid <= 0;
        while (!rvalid) @(posedge clk);
        d = rdata;
        @(posedge clk); rready <= 0;
    endtask
    task automatic expect_rd(input [11:0] a, input [31:0] e, input string what);
        logic [31:0] d; rd(a, d);
        if (d !== e) begin errors++; $display("REG MISMATCH %s addr=%h got=%h exp=%h", what, a, d, e); end
    endtask
    task automatic check_capture(input bit sel, input [15:0] thr);
        logic [31:0] st, d0, d1, d2, d3; logic [127:0] e; int n;
        expq.delete();
        wr(12'h044, thr);
        wr(12'h040, {30'd0, sel, 1'b1});
        n = 0;
        do begin rd(12'h018, st); n++; end while (!st[2] && n < 200000);
        if (!st[2]) begin errors++; $display("CAPTURE never finished"); end
        if (expq.size() != NCAP) begin errors++; $display("CAPTURE count %0d", expq.size()); end
        for (int k = 0; k < NCAP; k++) begin
            wr(12'h048, k);
            rd(12'h050, d0); rd(12'h054, d1); rd(12'h058, d2); rd(12'h05C, d3);
            e = expq[k];
            if ({d3, d2, d1, d0} !== e) begin errors++; if (errors < 10) $display("CAPTURE MISMATCH k=%0d", k); end
        end
        if (thr != 0) begin
            e = expq[0];
            if (($signed(e[15:0]) < 0 ? -$signed(e[15:0]) : $signed(e[15:0])) +
                ($signed(e[31:16]) < 0 ? -$signed(e[31:16]) : $signed(e[31:16])) <= thr && !sel) begin
                errors++; $display("CAPTURE threshold not honoured");
            end
        end
    endtask

    logic [31:0] v, lo, hi, tg0, tg1;
    logic signed [17:0] cre [0:3], cim [0:3];
    initial begin
        repeat (10) @(posedge clk); rstn <= 1; repeat (10) @(posedge clk);
        expect_rd(12'h000, 32'h51C0_0001, "ID");
        expect_rd(12'h004, {8'd15, 8'd18, 8'd12, 8'd4}, "INFO");
        expect_rd(12'h008, 0, "CTRL after reset");
        repeat (300) @(posedge clk);                 // bypass after reset, compared against refm
        for (int k = 0; k < 4; k++) begin
            cre[k] = $urandom; cim[k] = $urandom;
            wr(12'h100 + 8 * k, 32'(cre[k])); wr(12'h104 + 8 * k, 32'(cim[k]));
        end
        for (int k = 0; k < 4; k++) begin
            expect_rd(12'h100 + 8 * k, 32'(cre[k]), "COEF_RE"); expect_rd(12'h104 + 8 * k, 32'(cim[k]), "COEF_IM");
        end
        wr(12'h010, 21); expect_rd(12'h010, 21, "DELAY");
        wr(12'h00C, 1);  expect_rd(12'h014, 1, "EPOCH");
        wr(12'h008, 1);  expect_rd(12'h008, 1, "CTRL");
        repeat (600) @(posedge clk);
        check_capture(1'b0, 16'd0);
        check_capture(1'b0, 16'd60000);
        check_capture(1'b1, 16'd0);
        // monitor read-back must equal the reference monitor when no latch happened in between
        for (int t = 0; t < 4; t++) begin
            do begin
                rd(12'h018, tg0); rd(12'h020, lo); rd(12'h024, hi); rd(12'h018, tg1);
            end while (tg0[0] !== tg1[0]);
            // the reference latch may have moved on; accept only a stable pair
            if (tg1[0] === rtog && {hi, lo} !== rpin) begin
                rd(12'h018, tg1);
                if (tg1[0] === tg0[0]) begin errors++; $display("MONITOR MISMATCH"); end
            end
        end
        // change coefficients and delay-free commit while running, then disable
        wr(12'h100, 32'h0000_1234); wr(12'h104, 32'hFFFF_F000); wr(12'h00C, 1);
        expect_rd(12'h014, 2, "EPOCH 2");
        repeat (400) @(posedge clk);
        wr(12'h008, 0);
        repeat (400) @(posedge clk);
        if (dut.u_sic.cfg_epoch !== repoch) errors++;
        $display("AXIL_TB compared=%0d errors=%0d %s", compared, errors, errors == 0 ? "PASS" : "FAIL");
        $finish;
    end
endmodule
