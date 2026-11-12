`timescale 1ns / 1ps

// Remove IEEE 802.3 padding from a received IPv4 Ethernet frame before it
// enters NetworkLayer.
//
// CMAC removes FCS but preserves Ethernet padding.  NetworkLayer v1.0 passes
// that padding through as UDP application bytes, so a one-byte UDP datagram is
// incorrectly delivered as 18 bytes after minimum-frame padding.  For an
// untagged IPv4 frame, the real pre-FCS length is:
//
//     14-byte Ethernet header + IPv4 Total Length
//
// A padded minimum frame is at most 60 bytes and therefore fits in one
// 512-bit AXI4-Stream beat.  This module shortens TKEEP only for such a valid
// single-beat IPv4 frame.  TDATA, TLAST, all non-IPv4 traffic and all
// multi-beat traffic pass through unchanged.
module helix_ipv4_eth_pad_stripper #(
    parameter integer DATA_WIDTH = 512,
    parameter integer KEEP_WIDTH = DATA_WIDTH / 8
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS:M_AXIS, ASSOCIATED_RESET aresetn" *)
    input  wire                      aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST", X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                      aresetn,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TDATA" *)
    input  wire [DATA_WIDTH-1:0]     S_AXIS_TDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TKEEP" *)
    input  wire [KEEP_WIDTH-1:0]     S_AXIS_TKEEP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TLAST" *)
    input  wire                      S_AXIS_TLAST,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TVALID" *)
    input  wire                      S_AXIS_TVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS TREADY" *)
    output wire                      S_AXIS_TREADY,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TDATA" *)
    output wire [DATA_WIDTH-1:0]     M_AXIS_TDATA,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TKEEP" *)
    output wire [KEEP_WIDTH-1:0]     M_AXIS_TKEEP,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TLAST" *)
    output wire                      M_AXIS_TLAST,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TVALID" *)
    output wire                      M_AXIS_TVALID,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS TREADY" *)
    input  wire                      M_AXIS_TREADY
);

    reg in_frame;

    wire transfer = S_AXIS_TVALID && S_AXIS_TREADY;
    wire single_beat_last = S_AXIS_TLAST && !in_frame;
    wire header_valid = &S_AXIS_TKEEP[17:0];
    wire untagged_ipv4 =
        (S_AXIS_TDATA[12 * 8 +: 8] == 8'h08) &&
        (S_AXIS_TDATA[13 * 8 +: 8] == 8'h00) &&
        (S_AXIS_TDATA[14 * 8 + 4 +: 4] == 4'h4);
    wire [15:0] ipv4_total_length = {
        S_AXIS_TDATA[16 * 8 +: 8],
        S_AXIS_TDATA[17 * 8 +: 8]
    };
    wire [16:0] ipv4_frame_bytes = {1'b0, ipv4_total_length} + 17'd14;

    // The range test makes the variable shift well-defined and limits this
    // combinational path to the legal one-beat IPv4 case.
    wire candidate_length =
        (ipv4_frame_bytes >= 17'd34) &&
        (ipv4_frame_bytes <= 17'd60);
    wire [6:0] keep_byte_count =
        candidate_length ? ipv4_frame_bytes[6:0] : KEEP_WIDTH;
    wire [KEEP_WIDTH-1:0] ipv4_keep =
        {KEEP_WIDTH{1'b1}} >> (KEEP_WIDTH - keep_byte_count);
    wire intended_bytes_present =
        (S_AXIS_TKEEP & ipv4_keep) == ipv4_keep;
    wire padding_present = |(S_AXIS_TKEEP & ~ipv4_keep);
    wire trim_padding = single_beat_last && header_valid && untagged_ipv4 &&
                        candidate_length && intended_bytes_present &&
                        padding_present;

    assign S_AXIS_TREADY = M_AXIS_TREADY;
    assign M_AXIS_TDATA  = S_AXIS_TDATA;
    assign M_AXIS_TKEEP  = trim_padding ? ipv4_keep : S_AXIS_TKEEP;
    assign M_AXIS_TLAST  = S_AXIS_TLAST;
    assign M_AXIS_TVALID = S_AXIS_TVALID;

    always @(posedge aclk) begin
        if (!aresetn) begin
            in_frame <= 1'b0;
        end else if (transfer) begin
            if (S_AXIS_TLAST) begin
                in_frame <= 1'b0;
            end else begin
                in_frame <= 1'b1;
            end
        end
    end

endmodule
