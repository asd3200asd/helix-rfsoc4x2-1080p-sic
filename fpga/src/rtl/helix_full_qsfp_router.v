`timescale 1ns/1ps

// Restore the complete HELIX UDP contract on top of NetworkLayer sockets.
//
// Socket 0: HELIX RF/data payload (UDP 10001)
// Socket 1: PS control payload       (UDP 10000)
// Socket 2: receiver metadata        (UDP 10002)
//
// mode 0: QSFP data -> HELIX; HELIX data + PS control + metadata -> QSFP
// mode 1: PS DMA -> HELIX; HELIX data + metadata -> QSFP
// mode 2: QSFP data -> HELIX; PS DMA data + metadata -> QSFP
// mode 3: stop all routed payloads
//
// The transmit arbiter locks its source until TLAST. Mode changes are applied
// only between receive and transmit packets.
module helix_full_qsfp_router #(
    parameter [15:0] DATA_SOCKET_ID     = 16'd0,
    parameter [15:0] CONTROL_SOCKET_ID  = 16'd1,
    parameter [15:0] METADATA_SOCKET_ID = 16'd2
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXIS_QSFP_DATA:S_AXIS_PS:S_AXIS_HELIX_TX:S_AXIS_METADATA:M_AXIS_HELIX:M_AXIS_QSFP, ASSOCIATED_RESET aresetn" *)
    input  wire         aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST", X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire         aresetn,

    input  wire [2:0]   control_async,
    output wire [7:0]   status,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP_DATA TDATA" *)
    input  wire [63:0]  s_axis_qsfp_data_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP_DATA TVALID" *)
    input  wire         s_axis_qsfp_data_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP_DATA TREADY" *)
    output wire         s_axis_qsfp_data_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_QSFP_DATA TLAST" *)
    input  wire         s_axis_qsfp_data_tlast,

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

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TDATA" *)
    input  wire [63:0]  s_axis_helix_tx_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TUSER" *)
    input  wire [1:0]   s_axis_helix_tx_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TVALID" *)
    input  wire         s_axis_helix_tx_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TREADY" *)
    output wire         s_axis_helix_tx_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_HELIX_TX TLAST" *)
    input  wire         s_axis_helix_tx_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_METADATA TDATA" *)
    input  wire [63:0]  s_axis_metadata_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_METADATA TUSER" *)
    input  wire [1:0]   s_axis_metadata_tuser,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_METADATA TVALID" *)
    input  wire         s_axis_metadata_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_METADATA TREADY" *)
    output wire         s_axis_metadata_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 S_AXIS_METADATA TLAST" *)
    input  wire         s_axis_metadata_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TDATA" *)
    output wire [63:0]  m_axis_helix_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TVALID" *)
    output wire         m_axis_helix_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TREADY" *)
    input  wire         m_axis_helix_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_HELIX TLAST" *)
    output wire         m_axis_helix_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TDATA" *)
    output reg  [63:0]  m_axis_qsfp_tdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TKEEP" *)
    output reg  [7:0]   m_axis_qsfp_tkeep,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TDEST" *)
    output reg  [15:0]  m_axis_qsfp_tdest,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TVALID" *)
    output reg          m_axis_qsfp_tvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TREADY" *)
    input  wire         m_axis_qsfp_tready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 M_AXIS_QSFP TLAST" *)
    output reg          m_axis_qsfp_tlast
);

    localparam [1:0] SRC_DATA = 2'd0;
    localparam [1:0] SRC_CTRL = 2'd1;
    localparam [1:0] SRC_META = 2'd2;
    localparam [1:0] SRC_NONE = 2'd3;

    reg [2:0] control_meta;
    reg [2:0] control_sync;
    reg [2:0] control_sync_d;
    reg [1:0] active_mode;
    reg       rx_packet_active;
    reg       tx_packet_active;
    reg [1:0] tx_grant;
    reg [1:0] rr_pointer;
    reg       keep_error_sticky;

    // Accept a new multi-bit mode only after two synchronized samples agree.
    // This prevents a 00->11 GPIO transition from briefly committing 01/10.
    wire [1:0] requested_mode = control_sync_d[1:0];
    wire       requested_mode_stable =
        (control_sync[1:0] == control_sync_d[1:0]);
    wire       clear_status_pulse = control_sync[2] && !control_sync_d[2];
    wire       mode_pending = (requested_mode != active_mode);
    wire       mode_production  = (active_mode == 2'd0);
    wire       mode_ps_to_helix = (active_mode == 2'd1);
    wire       mode_ps_to_qsfp  = (active_mode == 2'd2);
    wire       mode_stopped     = (active_mode == 2'd3);

    wire route_qsfp_to_helix = mode_production || mode_ps_to_qsfp;
    wire route_ps_to_helix   = mode_ps_to_helix;

    wire [63:0] ps_masked_data;
    genvar byte_index;
    generate
        for (byte_index = 0; byte_index < 8; byte_index = byte_index + 1) begin : gen_ps_keep_mask
            assign ps_masked_data[byte_index*8 +: 8] =
                s_axis_ps_tdata[byte_index*8 +: 8] & {8{s_axis_ps_tkeep[byte_index]}};
        end
    endgenerate

    assign m_axis_helix_tdata  = route_ps_to_helix ? ps_masked_data : s_axis_qsfp_data_tdata;
    assign m_axis_helix_tvalid = (route_ps_to_helix && s_axis_ps_tvalid) ||
                                 (route_qsfp_to_helix && s_axis_qsfp_data_tvalid);
    assign m_axis_helix_tlast  = route_ps_to_helix ? s_axis_ps_tlast : s_axis_qsfp_data_tlast;
    assign s_axis_qsfp_data_tready = route_qsfp_to_helix ? m_axis_helix_tready : 1'b0;

    wire tx_data_valid = (mode_production || mode_ps_to_helix) ? s_axis_helix_tx_tvalid :
                         mode_ps_to_qsfp ? s_axis_ps_tvalid : 1'b0;
    wire tx_ctrl_valid = mode_production && s_axis_ps_tvalid;
    wire tx_meta_valid = !mode_stopped && s_axis_metadata_tvalid;

    reg [1:0] idle_choice;
    always @* begin
        idle_choice = SRC_NONE;
        case (rr_pointer)
            SRC_DATA: begin
                if (tx_data_valid)      idle_choice = SRC_DATA;
                else if (tx_ctrl_valid) idle_choice = SRC_CTRL;
                else if (tx_meta_valid) idle_choice = SRC_META;
            end
            SRC_CTRL: begin
                if (tx_ctrl_valid)      idle_choice = SRC_CTRL;
                else if (tx_meta_valid) idle_choice = SRC_META;
                else if (tx_data_valid) idle_choice = SRC_DATA;
            end
            default: begin
                if (tx_meta_valid)      idle_choice = SRC_META;
                else if (tx_data_valid) idle_choice = SRC_DATA;
                else if (tx_ctrl_valid) idle_choice = SRC_CTRL;
            end
        endcase
    end

    wire [1:0] selected_source = tx_packet_active ? tx_grant : idle_choice;
    wire       select_data = (selected_source == 2'd0);
    wire       select_ctrl = (selected_source == 2'd1);
    wire       select_meta = (selected_source == 2'd2);
    wire       data_is_ps  = mode_ps_to_qsfp;

    always @* begin
        m_axis_qsfp_tdata  = 64'd0;
        m_axis_qsfp_tkeep  = 8'd0;
        m_axis_qsfp_tdest  = 16'd0;
        m_axis_qsfp_tvalid = 1'b0;
        m_axis_qsfp_tlast  = 1'b0;
        case (selected_source)
            SRC_DATA: begin
                m_axis_qsfp_tdata  = data_is_ps ? s_axis_ps_tdata  : s_axis_helix_tx_tdata;
                m_axis_qsfp_tkeep  = data_is_ps ? s_axis_ps_tkeep  : 8'hff;
                m_axis_qsfp_tdest  = DATA_SOCKET_ID;
                m_axis_qsfp_tvalid = data_is_ps ? s_axis_ps_tvalid : s_axis_helix_tx_tvalid;
                m_axis_qsfp_tlast  = data_is_ps ? s_axis_ps_tlast  : s_axis_helix_tx_tlast;
            end
            SRC_CTRL: begin
                m_axis_qsfp_tdata  = s_axis_ps_tdata;
                m_axis_qsfp_tkeep  = s_axis_ps_tkeep;
                m_axis_qsfp_tdest  = CONTROL_SOCKET_ID;
                m_axis_qsfp_tvalid = s_axis_ps_tvalid;
                m_axis_qsfp_tlast  = s_axis_ps_tlast;
            end
            SRC_META: begin
                m_axis_qsfp_tdata  = s_axis_metadata_tdata;
                m_axis_qsfp_tkeep  = 8'hff;
                m_axis_qsfp_tdest  = METADATA_SOCKET_ID;
                m_axis_qsfp_tvalid = s_axis_metadata_tvalid;
                m_axis_qsfp_tlast  = s_axis_metadata_tlast;
            end
            default: begin end
        endcase
    end

    assign s_axis_helix_tx_tready =
        select_data && !data_is_ps ? m_axis_qsfp_tready : 1'b0;
    assign s_axis_metadata_tready =
        select_meta ? m_axis_qsfp_tready : 1'b0;
    assign s_axis_ps_tready = route_ps_to_helix ? m_axis_helix_tready :
                              ((select_ctrl || (select_data && data_is_ps)) ?
                               m_axis_qsfp_tready : 1'b0);

    wire rx_accept = m_axis_helix_tvalid && m_axis_helix_tready;
    wire tx_accept = m_axis_qsfp_tvalid && m_axis_qsfp_tready;
    wire ps_to_helix_accept = route_ps_to_helix && s_axis_ps_tvalid && s_axis_ps_tready;
    wire unused_helix_user = ^s_axis_helix_tx_tuser;
    wire unused_metadata_user = ^s_axis_metadata_tuser;

    assign status = {
        s_axis_ps_tvalid,
        control_sync[2],
        keep_error_sticky,
        tx_packet_active,
        rx_packet_active,
        mode_pending,
        active_mode
    };

    always @(posedge aclk) begin
        if (!aresetn) begin
            control_meta       <= 3'b000;
            control_sync       <= 3'b000;
            control_sync_d     <= 3'b000;
            active_mode        <= 2'd0;
            rx_packet_active   <= 1'b0;
            tx_packet_active   <= 1'b0;
            tx_grant           <= SRC_NONE;
            rr_pointer         <= SRC_DATA;
            keep_error_sticky  <= 1'b0;
        end else begin
            control_meta   <= control_async;
            control_sync   <= control_meta;
            control_sync_d <= control_sync;

            // TVALID already presented with TREADY low is an in-flight AXIS
            // transfer even before the first handshake.  Do not change the
            // mux in that state: TDATA/TLAST/TDEST must remain stable.
            if (requested_mode_stable &&
                !rx_packet_active && !tx_packet_active &&
                !m_axis_helix_tvalid && !m_axis_qsfp_tvalid)
                active_mode <= requested_mode;

            if (rx_accept)
                rx_packet_active <= !m_axis_helix_tlast;

            // Lock a source as soon as it presents TVALID, not only after
            // TREADY.  Otherwise a newly-valid higher-priority source could
            // change TDATA/TDEST while the output is backpressured.
            if (!tx_packet_active && (idle_choice != SRC_NONE)) begin
                tx_grant         <= idle_choice;
                tx_packet_active <= 1'b1;
            end

            if (tx_accept) begin
                if (!tx_packet_active)
                    tx_grant <= idle_choice;
                if (m_axis_qsfp_tlast)
                    tx_packet_active <= 1'b0;
                else
                    tx_packet_active <= 1'b1;
                if (m_axis_qsfp_tlast) begin
                    case (selected_source)
                        SRC_DATA: rr_pointer <= SRC_CTRL;
                        SRC_CTRL: rr_pointer <= SRC_META;
                        default:  rr_pointer <= SRC_DATA;
                    endcase
                end
            end

            if (ps_to_helix_accept && (s_axis_ps_tkeep != 8'hff))
                keep_error_sticky <= 1'b1;
            else if (clear_status_pulse)
                keep_error_sticky <= 1'b0;
        end
    end

endmodule
