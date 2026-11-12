`timescale 1ns/1ps

// Packet-safe routing between the PS DMA, the QSFP UDP payload path, and
// HELIX's Stream Manager.  Mode changes are synchronized into aclk and are
// applied only while both outputs are between packets.
//
// mode 0: QSFP RX -> HELIX, HELIX -> QSFP TX (production)
// mode 1: PS DMA  -> HELIX, HELIX -> QSFP TX (local PS injection)
// mode 2: QSFP RX -> HELIX, PS DMA -> QSFP TX (PS UDP transmission)
// mode 3: all routes stopped
//
// HELIX does not expose TKEEP on its UDP-side AXIS.  In mode 1, invalid PS
// bytes are zero-filled and a sticky alignment error is raised.  Software
// must normally pad PS-to-HELIX buffers to an 8-byte boundary.  Mode 2 keeps
// the DMA TKEEP unchanged, so arbitrary UDP payload lengths are supported.
module helix_ps_qsfp_router (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS_QSFP:S_AXIS_PS:S_AXIS_HELIX_TX:M_AXIS_HELIX:M_AXIS_QSFP, ASSOCIATED_RESET aresetn" *)
    input  wire         aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST", X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire         aresetn,

    // gpio[1:0] = requested mode, gpio[2] = clear sticky status (pulse).
    input  wire [2:0]   control_async,
    output wire [7:0]   status,

    // UDP payload received from the QSFP NetworkLayer after 512-to-64 width
    // conversion.  HELIX's input has the same no-TKEEP contract.
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP TDATA" *)
    input  wire [63:0]  s_axis_qsfp_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP TVALID" *)
    input  wire         s_axis_qsfp_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP TREADY" *)
    output wire         s_axis_qsfp_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP TLAST" *)
    input  wire         s_axis_qsfp_tlast,

    // PS DDR -> AXI DMA MM2S stream.
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_PS TDATA" *)
    input  wire [63:0]  s_axis_ps_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_PS TKEEP" *)
    input  wire [7:0]   s_axis_ps_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_PS TVALID" *)
    input  wire         s_axis_ps_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_PS TREADY" *)
    output wire         s_axis_ps_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_PS TLAST" *)
    input  wire         s_axis_ps_tlast,

    // HELIX payload that would normally be transmitted over UDP.
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TDATA" *)
    input  wire [63:0]  s_axis_helix_tx_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TVALID" *)
    input  wire         s_axis_helix_tx_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TREADY" *)
    output wire         s_axis_helix_tx_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TLAST" *)
    input  wire         s_axis_helix_tx_tlast,

    // Selected payload into HELIX Stream Manager S00_UDP_AXIS.
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TDATA" *)
    output wire [63:0]  m_axis_helix_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TVALID" *)
    output wire         m_axis_helix_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TREADY" *)
    input  wire         m_axis_helix_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TLAST" *)
    output wire         m_axis_helix_tlast,

    // Selected payload toward the QSFP NetworkLayer socket input.
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TDATA" *)
    output wire [63:0]  m_axis_qsfp_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TKEEP" *)
    output wire [7:0]   m_axis_qsfp_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TVALID" *)
    output wire         m_axis_qsfp_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TREADY" *)
    input  wire         m_axis_qsfp_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TLAST" *)
    output wire         m_axis_qsfp_tlast
);

    reg [2:0] control_meta;
    reg [2:0] control_sync;
    reg [2:0] control_sync_d;
    reg [1:0] active_mode;
    reg       helix_packet_active;
    reg       qsfp_packet_active;
    reg       keep_error_sticky;

    wire [1:0] requested_mode = control_sync[1:0];
    wire       clear_status_pulse = control_sync[2] && !control_sync_d[2];
    wire       mode_pending = (requested_mode != active_mode);

    wire mode_production  = (active_mode == 2'd0);
    wire mode_ps_to_helix = (active_mode == 2'd1);
    wire mode_ps_to_qsfp  = (active_mode == 2'd2);

    wire route_qsfp_to_helix = mode_production || mode_ps_to_qsfp;
    wire route_ps_to_helix   = mode_ps_to_helix;
    wire route_helix_to_qsfp = mode_production || mode_ps_to_helix;
    wire route_ps_to_qsfp    = mode_ps_to_qsfp;

    wire [63:0] ps_masked_data;
    genvar byte_index;
    generate
        for (byte_index = 0; byte_index < 8; byte_index = byte_index + 1) begin : gen_ps_keep_mask
            assign ps_masked_data[byte_index*8 +: 8] =
                s_axis_ps_tdata[byte_index*8 +: 8] & {8{s_axis_ps_tkeep[byte_index]}};
        end
    endgenerate

    assign m_axis_helix_tdata = route_ps_to_helix ? ps_masked_data : s_axis_qsfp_tdata;
    assign m_axis_helix_tvalid =
        (route_ps_to_helix && s_axis_ps_tvalid) ||
        (route_qsfp_to_helix && s_axis_qsfp_tvalid);
    assign m_axis_helix_tlast = route_ps_to_helix ? s_axis_ps_tlast : s_axis_qsfp_tlast;

    assign m_axis_qsfp_tdata = route_ps_to_qsfp ? s_axis_ps_tdata : s_axis_helix_tx_tdata;
    assign m_axis_qsfp_tkeep = route_ps_to_qsfp ? s_axis_ps_tkeep : 8'hff;
    assign m_axis_qsfp_tvalid =
        (route_ps_to_qsfp && s_axis_ps_tvalid) ||
        (route_helix_to_qsfp && s_axis_helix_tx_tvalid);
    assign m_axis_qsfp_tlast = route_ps_to_qsfp ? s_axis_ps_tlast : s_axis_helix_tx_tlast;

    assign s_axis_qsfp_tready = route_qsfp_to_helix ? m_axis_helix_tready : 1'b0;
    assign s_axis_helix_tx_tready = route_helix_to_qsfp ? m_axis_qsfp_tready : 1'b0;
    assign s_axis_ps_tready = route_ps_to_helix ? m_axis_helix_tready :
                              route_ps_to_qsfp  ? m_axis_qsfp_tready  : 1'b0;

    wire helix_accept = m_axis_helix_tvalid && m_axis_helix_tready;
    wire qsfp_accept  = m_axis_qsfp_tvalid  && m_axis_qsfp_tready;
    wire ps_to_helix_accept = route_ps_to_helix && s_axis_ps_tvalid && s_axis_ps_tready;

    // status[1:0] active mode; [2] switch pending; [3]/[4] packet active;
    // [5] sticky PS-to-HELIX partial-TKEEP error; [6] clear request seen;
    // [7] PS stream currently valid.
    assign status = {
        s_axis_ps_tvalid,
        control_sync[2],
        keep_error_sticky,
        qsfp_packet_active,
        helix_packet_active,
        mode_pending,
        active_mode
    };

    always @(posedge aclk) begin
        if (!aresetn) begin
            control_meta        <= 3'b000;
            control_sync        <= 3'b000;
            control_sync_d      <= 3'b000;
            active_mode         <= 2'd0;
            helix_packet_active <= 1'b0;
            qsfp_packet_active  <= 1'b0;
            keep_error_sticky   <= 1'b0;
        end else begin
            control_meta   <= control_async;
            control_sync   <= control_meta;
            control_sync_d <= control_sync;

            // Never move a source between destinations during a packet.  Also
            // avoid changing mode on a cycle accepting the first packet beat.
            if (!helix_packet_active && !qsfp_packet_active &&
                !helix_accept && !qsfp_accept)
                active_mode <= requested_mode;

            if (helix_accept)
                helix_packet_active <= !m_axis_helix_tlast;
            if (qsfp_accept)
                qsfp_packet_active <= !m_axis_qsfp_tlast;

            if (ps_to_helix_accept && (s_axis_ps_tkeep != 8'hff))
                keep_error_sticky <= 1'b1;
            else if (clear_status_pulse)
                keep_error_sticky <= 1'b0;
        end
    end

endmodule
