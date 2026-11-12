`timescale 1ns/1ps

// Forward only one NetworkLayer socket to HELIX and discard all other packets.
// NetworkLayer RX TUSER metadata is intentionally consumed but not forwarded,
// because the HELIX stream manager accepts payload only.
module helix_qsfp_rx_filter #(
    parameter [15:0] SOCKET_ID = 16'd0
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:M_AXIS, ASSOCIATED_RESET aresetn" *)
    input  wire          aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST", X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire          aresetn,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDATA" *)
    input  wire [511:0]  s_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TKEEP" *)
    input  wire [63:0]   s_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDEST" *)
    input  wire [15:0]   s_axis_tdest,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TUSER" *)
    input  wire [95:0]   s_axis_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TVALID" *)
    input  wire          s_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TREADY" *)
    output wire          s_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TLAST" *)
    input  wire          s_axis_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TDATA" *)
    output wire [511:0]  m_axis_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TKEEP" *)
    output wire [63:0]   m_axis_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TVALID" *)
    output wire          m_axis_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TREADY" *)
    input  wire          m_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TLAST" *)
    output wire          m_axis_tlast
);

    wire unused_metadata = ^s_axis_tuser;

    reg in_drop_packet;
    wire socket_match = (s_axis_tdest == SOCKET_ID);
    wire forward_beat = !in_drop_packet && socket_match;
    wire accept_beat  = s_axis_tvalid && s_axis_tready;

    assign m_axis_tdata  = s_axis_tdata;
    assign m_axis_tkeep  = s_axis_tkeep;
    assign m_axis_tlast  = s_axis_tlast;
    assign m_axis_tvalid = s_axis_tvalid && forward_beat;
    assign s_axis_tready = forward_beat ? m_axis_tready : 1'b1;

    always @(posedge aclk) begin
        if (!aresetn) begin
            in_drop_packet <= 1'b0;
        end else if (accept_beat) begin
            if (s_axis_tlast)
                in_drop_packet <= 1'b0;
            else if (in_drop_packet || !socket_match)
                in_drop_packet <= 1'b1;
        end
    end

endmodule
