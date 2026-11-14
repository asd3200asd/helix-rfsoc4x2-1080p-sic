`timescale 1ns/1ps
module tb;
  parameter LATCH = 1;
  reg clk = 0, arst = 0; always #5 clk = ~clk;
  reg trigger = 0; reg [31:0] ts = 0; reg sv = 0; reg [63:0] sd = 0; integer avail = 0, outn = 0, lastn = 0, cyc = 0;
  wire sr, mv, ml; wire [63:0] md;
  packet_fragmentation #(.LATCH_TRIGGER(LATCH)) dut(.clk(clk), .arst(arst), .s_axis_tdata(sd), .s_axis_tvalid(sv), .s_axis_tready(sr),
    .s_axis_tlast(1'b0), .s_axis_tuser(1'b0), .m_axis_tdata(md), .m_axis_tvalid(mv), .m_axis_tready(1'b1), .m_axis_tlast(ml), .m_axis_tuser(),
    .trigger(trigger), .side_info_enabled(1'b0), .side_info_completed(1'b0), .mss(32'd4), .ifg(32'd3), .transfer_size(ts),
    .transfer_init(), .transfer_completed(), .slot_processed(), .reset_out());
  // source: 'avail' beats queued; presents one beat about every 3rd cycle (slow data delivery)
  always @(posedge clk) begin cyc <= cyc + 1;
    if (sv && sr) begin sd <= sd + 1; avail <= avail - 1; end
    sv <= (avail - ((sv && sr) ? 1 : 0)) > 0 && (cyc % 3 == 0);
    if (mv) begin outn <= outn + 1; if (md != outn) $display("ORDER ERR at %0d got %0d", outn, md); end end
  task trig(input [31:0] n); begin @(posedge clk) trigger <= 0; repeat (2) @(posedge clk); ts <= n; @(posedge clk) trigger <= 1; avail = avail + n; end endtask
  initial begin repeat (4) @(posedge clk); arst <= 1; repeat (3) @(posedge clk);
    trig(8); repeat (12) @(posedge clk);         // second trigger while the first transfer is still running
    trig(12); repeat (400) @(posedge clk);
    $display("RESULT LATCH=%0d out=%0d expected=20 fifo_left=%0d", LATCH, outn, avail);
    trig(4); repeat (200) @(posedge clk);       // a later normal trigger after idle
    $display("RESULT2 LATCH=%0d out=%0d expected=24", LATCH, outn); $finish; end
endmodule