`timescale 1ns / 1ps

// Add IEEE 802.3 padding after NetworkLayer and before the CMAC.
//
// NetworkLayer produces complete Ethernet frames without FCS on a 512-bit
// AXI4-Stream.  The CMAC inserts four FCS bytes, so a legal minimum frame must
// contain at least 60 input bytes.  A frame shorter than that necessarily fits
// in one 64-byte beat.  This module extends only such single-beat frames;
// multi-beat frames and their final partial beat pass through unchanged.
//
// The IP/UDP length fields are already inside TDATA and remain unchanged.
// Consequently the inserted bytes are Ethernet padding, not UDP payload.
module helix_eth_min_frame_padder #(
    parameter integer DATA_WIDTH      = 512,
    parameter integer KEEP_WIDTH      = DATA_WIDTH / 8,
    parameter integer MIN_FRAME_BYTES = 60
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

    localparam [KEEP_WIDTH-1:0] MIN_FRAME_KEEP =
        {KEEP_WIDTH{1'b1}} >> (KEEP_WIDTH - MIN_FRAME_BYTES);

    reg in_frame;
    reg [DATA_WIDTH-1:0] padded_data;
    integer byte_index;

    wire transfer = S_AXIS_TVALID && S_AXIS_TREADY;
    wire single_beat_last = S_AXIS_TLAST && !in_frame;

    assign S_AXIS_TREADY = M_AXIS_TREADY;
    assign M_AXIS_TVALID = S_AXIS_TVALID;
    assign M_AXIS_TLAST  = S_AXIS_TLAST;
    assign M_AXIS_TKEEP  = single_beat_last ?
                           (S_AXIS_TKEEP | MIN_FRAME_KEEP) : S_AXIS_TKEEP;
    assign M_AXIS_TDATA  = padded_data;

    always @* begin
        padded_data = S_AXIS_TDATA;
        if (single_beat_last) begin
            for (byte_index = 0; byte_index < MIN_FRAME_BYTES;
                 byte_index = byte_index + 1) begin
                if (!S_AXIS_TKEEP[byte_index]) begin
                    padded_data[byte_index * 8 +: 8] = 8'h00;
                end
            end
        end
    end

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
