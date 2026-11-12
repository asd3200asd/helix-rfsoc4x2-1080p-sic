// sic_axil_bd.v -- plain-Verilog shell so the block design can use it as a module reference.
`timescale 1ns/1ps
module sic_axil_bd #(
    parameter integer TAPS   = 4,
    parameter integer DW     = 12,
    parameter integer WLOG   = 22,
    parameter integer CAP_AW = 12
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi:s_axis_i:s_axis_q:m_axis_i:m_axis_q, ASSOCIATED_RESET aresetn" *)
    input  wire        aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        aresetn,
    input  wire [11:0] s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output wire        s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [11:0] s_axi_araddr,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output wire [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output wire        s_axi_rvalid,
    input  wire        s_axi_rready,
    // RX samples from the RFDC (I and Q streams), free running
    input  wire [31:0] s_axis_i_tdata,
    input  wire        s_axis_i_tvalid,
    output wire        s_axis_i_tready,
    input  wire [31:0] s_axis_q_tdata,
    input  wire        s_axis_q_tvalid,
    output wire        s_axis_q_tready,
    // to the RX PHY
    output wire [31:0] m_axis_i_tdata,
    output wire        m_axis_i_tvalid,
    input  wire        m_axis_i_tready,
    output wire [31:0] m_axis_q_tdata,
    output wire        m_axis_q_tvalid,
    input  wire        m_axis_q_tready,
    // TX reference: the word presented to the RF-DAC on every dac_clk
    input  wire        dac_clk,
    input  wire [63:0] dac_tdata
);
    wire out_valid;
    assign s_axis_i_tready = 1'b1;
    assign s_axis_q_tready = 1'b1;
    assign m_axis_i_tvalid = out_valid;
    assign m_axis_q_tvalid = out_valid;
    sic_axil #(.TAPS(TAPS), .DW(DW), .WLOG(WLOG), .CAP_AW(CAP_AW)) u_sic (
        .aclk(aclk), .aresetn(aresetn),
        .s_axi_awaddr(s_axi_awaddr), .s_axi_awvalid(s_axi_awvalid), .s_axi_awready(s_axi_awready),
        .s_axi_wdata(s_axi_wdata), .s_axi_wstrb(s_axi_wstrb), .s_axi_wvalid(s_axi_wvalid), .s_axi_wready(s_axi_wready),
        .s_axi_bresp(s_axi_bresp), .s_axi_bvalid(s_axi_bvalid), .s_axi_bready(s_axi_bready),
        .s_axi_araddr(s_axi_araddr), .s_axi_arvalid(s_axi_arvalid), .s_axi_arready(s_axi_arready),
        .s_axi_rdata(s_axi_rdata), .s_axi_rresp(s_axi_rresp), .s_axi_rvalid(s_axi_rvalid), .s_axi_rready(s_axi_rready),
        .dac_clk(dac_clk), .dac_tdata(dac_tdata),
        .adc_i(s_axis_i_tdata), .adc_q(s_axis_q_tdata), .adc_valid(s_axis_i_tvalid & s_axis_q_tvalid),
        .out_i(m_axis_i_tdata), .out_q(m_axis_q_tdata), .out_valid(out_valid));
endmodule
