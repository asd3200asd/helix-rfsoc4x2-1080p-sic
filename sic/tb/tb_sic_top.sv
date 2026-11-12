// File-driven testbench. One stimulus row per clock:
//   yi0 yq0 yi1 yq1 xi0 xq0 xi1 xq1 en commit we addr re im
// One output row per clock:
//   ei0 eq0 ei1 eq1 di0 dq0 di1 dq1 clip epoch mon_toggle pow_in pow_out clip_beats
`timescale 1ns/1ps
module tb_sic_top;
    parameter int TAPS = 4;
    parameter int DW   = 12;
    parameter int WLOG = 8;
    logic clk = 1'b0;
    always #2.034 clk = ~clk;
    logic rst_n = 1'b0;
    logic [63:0] dac_tdata = '0;
    logic [31:0] adc_i = '0, adc_q = '0, out_i, out_q, dly_i, dly_q, cfg_epoch, clip_beats;
    logic cfg_we = 0, cfg_commit = 0, cfg_enable = 0, mon_toggle;
    logic [4:0] cfg_addr = '0;
    logic signed [17:0] cfg_re = '0, cfg_im = '0;
    logic [DW-1:0] cfg_delay = '0;
    logic [3:0] clip;
    logic [63:0] pow_in, pow_out;

    sic_top #(.TAPS(TAPS), .DW(DW), .WLOG(WLOG)) dut (
        .adc_clk(clk), .rst_n(rst_n), .dac_clk(clk), .dac_tdata(dac_tdata),
        .adc_i(adc_i), .adc_q(adc_q), .out_i(out_i), .out_q(out_q),
        .cfg_we(cfg_we), .cfg_addr(cfg_addr), .cfg_re(cfg_re), .cfg_im(cfg_im),
        .cfg_commit(cfg_commit), .cfg_enable(cfg_enable), .cfg_delay(cfg_delay),
        .cfg_epoch(cfg_epoch), .dly_i(dly_i), .dly_q(dly_q), .clip(clip),
        .pow_in(pow_in), .pow_out(pow_out), .clip_beats(clip_beats), .mon_toggle(mon_toggle));

    string stim_name, out_name;
    int fa, fi, fo, code, dly, flush;
    int v [0:13];
    bit running = 0, eof = 0;
    initial begin
        // tb_args.txt (current directory): <stimulus file> <output file> <cfg_delay>
        fa = $fopen("tb_args.txt", "r"); if (fa == 0) $fatal(1, "cannot open tb_args.txt");
        code = $fscanf(fa, "%s %s %d", stim_name, out_name, dly);
        if (code != 3) $fatal(1, "bad tb_args.txt");
        $fclose(fa);
        fi = $fopen(stim_name, "r"); if (fi == 0) $fatal(1, "cannot open stim");
        fo = $fopen(out_name, "w");  if (fo == 0) $fatal(1, "cannot open out");
        cfg_delay = dly[DW-1:0];
        repeat (8) @(posedge clk);
        rst_n <= 1'b1;
        repeat (4) @(posedge clk);
        running <= 1'b1;
    end
    always @(posedge clk) if (running) begin
        $fwrite(fo, "%0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d %0d\n",
            $signed(out_i[15:0]), $signed(out_q[15:0]), $signed(out_i[31:16]), $signed(out_q[31:16]),
            $signed(dly_i[15:0]), $signed(dly_q[15:0]), $signed(dly_i[31:16]), $signed(dly_q[31:16]),
            clip, cfg_epoch, mon_toggle, pow_in, pow_out, clip_beats);
        if (!eof) begin
            code = $fscanf(fi, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d",
                v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13]);
            if (code != 14) begin eof = 1; flush = 0; end
        end
        if (eof) begin
            adc_i <= '0; adc_q <= '0; dac_tdata <= '0;
            cfg_we <= 0; cfg_commit <= 0;
            flush = flush + 1;
            if (flush > 96) begin $fclose(fo); $display("TB_DONE"); $finish; end
        end else begin
            adc_i      <= {v[2][15:0], v[0][15:0]};
            adc_q      <= {v[3][15:0], v[1][15:0]};
            dac_tdata  <= {v[7][15:0], v[6][15:0], v[5][15:0], v[4][15:0]};
            cfg_enable <= v[8][0];
            cfg_commit <= v[9][0];
            cfg_we     <= v[10][0];
            cfg_addr   <= v[11][4:0];
            cfg_re     <= v[12][17:0];
            cfg_im     <= v[13][17:0];
        end
    end
endmodule

// One elaboration top per tap count (xelab -generic_top is awkward through cmd.exe).
module tb_t1;  tb_sic_top #(.TAPS(1))  u (); endmodule
module tb_t4;  tb_sic_top #(.TAPS(4))  u (); endmodule
module tb_t8;  tb_sic_top #(.TAPS(8))  u (); endmodule
module tb_t16; tb_sic_top #(.TAPS(16)) u (); endmodule
