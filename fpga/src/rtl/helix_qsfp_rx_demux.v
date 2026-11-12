`timescale 1ns/1ps

// Split the NetworkLayer socket stream into the two HELIX receive paths.
// Socket selection is latched at the first accepted beat and held through
// TLAST, so a malformed packet cannot move between outputs mid-packet.
module helix_qsfp_rx_demux #(
    parameter [15:0] DATA_SOCKET_ID    = 16'd0,
    parameter [15:0] CONTROL_SOCKET_ID = 16'd1
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:M_AXIS_DATA:M_AXIS_CONTROL, ASSOCIATED_RESET aresetn" *)
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

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DATA TDATA" *)
    output wire [511:0]  m_axis_data_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DATA TKEEP" *)
    output wire [63:0]   m_axis_data_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DATA TVALID" *)
    output wire          m_axis_data_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DATA TREADY" *)
    input  wire          m_axis_data_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_DATA TLAST" *)
    output wire          m_axis_data_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_CONTROL TDATA" *)
    output wire [511:0]  m_axis_control_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_CONTROL TKEEP" *)
    output wire [63:0]   m_axis_control_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_CONTROL TVALID" *)
    output wire          m_axis_control_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_CONTROL TREADY" *)
    input  wire          m_axis_control_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_CONTROL TLAST" *)
    output wire          m_axis_control_tlast
);

    localparam [1:0] SELECT_DATA    = 2'd0;
    localparam [1:0] SELECT_CONTROL = 2'd1;
    localparam [1:0] SELECT_DROP    = 2'd2;

    reg       packet_active;
    reg [1:0] packet_select;

    wire [1:0] first_select =
        (s_axis_tdest == DATA_SOCKET_ID)    ? SELECT_DATA :
        (s_axis_tdest == CONTROL_SOCKET_ID) ? SELECT_CONTROL : SELECT_DROP;
    wire [1:0] active_select = packet_active ? packet_select : first_select;
    wire       accept_beat = s_axis_tvalid && s_axis_tready;
    wire       unused_metadata = ^s_axis_tuser;

    assign m_axis_data_tdata  = s_axis_tdata;
    assign m_axis_data_tkeep  = s_axis_tkeep;
    assign m_axis_data_tlast  = s_axis_tlast;
    assign m_axis_data_tvalid = s_axis_tvalid && (active_select == SELECT_DATA);

    assign m_axis_control_tdata  = s_axis_tdata;
    assign m_axis_control_tkeep  = s_axis_tkeep;
    assign m_axis_control_tlast  = s_axis_tlast;
    assign m_axis_control_tvalid = s_axis_tvalid && (active_select == SELECT_CONTROL);

    assign s_axis_tready =
        (active_select == SELECT_DATA)    ? m_axis_data_tready :
        (active_select == SELECT_CONTROL) ? m_axis_control_tready : 1'b1;

    always @(posedge aclk) begin
        if (!aresetn) begin
            packet_active <= 1'b0;
            packet_select <= SELECT_DROP;
        end else if (accept_beat) begin
            if (s_axis_tlast) begin
                packet_active <= 1'b0;
            end else if (!packet_active) begin
                packet_active <= 1'b1;
                packet_select <= first_select;
            end
        end
    end

endmodule

