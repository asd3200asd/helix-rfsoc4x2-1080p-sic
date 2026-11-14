`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 09/27/2022 05:43:30 PM
// Design Name: 
// Module Name: udp_fragmentation
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


// Claude 2026-11-14 v1.1: LATCH_TRIGGER=1 (TX instance) remembers a trigger edge that arrives while a transfer is still running
// (the original only samples the edge in WAIT_TRIGGER, so such a trigger was lost, its data stayed in the FIFO and the back-pressure
// blocked the shared network input incl. control) and latches transfer_size at the edge (a size written during a transfer used to
// change the running count).  LATCH_TRIGGER=0 (RX instance) keeps the original behaviour.
module packet_fragmentation #(parameter LATCH_TRIGGER = 0) (

    input  wire        clk,
    input  wire        arst,
    
    input  wire [63:0] s_axis_tdata,
    input wire          s_axis_tvalid,
    output wire        s_axis_tready,
    input  wire        s_axis_tlast,
    input  wire        s_axis_tuser,
        
    output  wire [63:0] m_axis_tdata,
    output  wire        m_axis_tvalid,
    input wire        m_axis_tready,
    output  reg        m_axis_tlast,
    output  wire        m_axis_tuser,
    
    input wire  trigger,
    input wire  side_info_enabled,
    input wire  side_info_completed,
    input wire [31:0] mss,
    input wire [31:0] ifg,
    input wire [31:0] transfer_size,
    output reg transfer_init,
    output reg transfer_completed,
    output reg slot_processed,
    output reg reset_out,

    output wire busy

    );


// Params
localparam WAIT_TRIGGER = 3'b000;
localparam SEND = 3'b001;
localparam IDLE = 3'b010;

reg[31:0] transfer_size_count,mss_count,ifg_count;

reg[2:0] state_w = WAIT_TRIGGER;

reg wr_en, rd_en,fifo_res;

wire s_tready_reg, m_tvalid_reg,m_tready_reg,af;

assign s_axis_tready=(s_tready_reg == 'b1 & rd_en == 'b1);

assign m_axis_tvalid=(m_tvalid_reg == 'b1 & wr_en  == 'b1);
assign m_tready_reg = m_axis_tready && wr_en;


reg trigger_r,trigger_rising_edge;
reg trig_q, pend; reg [31:0] pend_ts, cur_ts;
wire trig_edge = trigger & ~trig_q;
wire [31:0] ts_eff = LATCH_TRIGGER ? cur_ts : transfer_size;
assign busy = (state_w != WAIT_TRIGGER) || pend;


always @ (posedge clk) begin
    if(!arst) begin
        transfer_size_count <=0;
        mss_count <=0;
        ifg_count <=0;
        wr_en<= 0;
        rd_en<= 0;
        m_axis_tlast <= 0;
        slot_processed <=0;
        state_w <= WAIT_TRIGGER;
        trig_q <= trigger; pend <= 0; pend_ts <= 0; cur_ts <= 0;
    end
    else begin
        trig_q <= trigger;
        if (LATCH_TRIGGER && trig_edge && state_w != WAIT_TRIGGER) begin pend <= 1; pend_ts <= transfer_size; end
        case(state_w)
            WAIT_TRIGGER: begin
                transfer_size_count <=0;
                mss_count <=0;
                ifg_count <=0;
                m_axis_tlast <= 0;
                wr_en<= 0;
                rd_en<= 0;
                slot_processed <=0;
                if (LATCH_TRIGGER) begin
                    if (trig_edge || pend) begin state_w <= SEND; cur_ts <= trig_edge ? transfer_size : pend_ts; pend <= 0; end
                end
                else if (trigger_rising_edge) begin
                    state_w <= SEND;
                    trigger_rising_edge <=0;
                end               
                else begin                
                    trigger_rising_edge <= !trigger_r && trigger;
                    trigger_r<=trigger;
                end
            end
            SEND: begin
                rd_en<= 1;
                wr_en<= 1;
                 slot_processed <=0;
                if(m_axis_tready && m_tvalid_reg && s_tready_reg) begin
                    transfer_size_count<= transfer_size_count+1;
                    mss_count<= mss_count+1;
                    if(transfer_size_count>=ts_eff-1) begin
                       rd_en<=0;
                       wr_en<=0;
                       m_axis_tlast <= 0;
                       slot_processed <=1;
                       transfer_completed <=1;
                       state_w <= WAIT_TRIGGER;
                    end
                    else if(mss_count>=mss-1) begin
                        mss_count<=0;
                        rd_en<=0;
                        wr_en<=0;
                        m_axis_tlast <= 0;
                        state_w <= IDLE;
                    end
                    else if(mss_count>=mss-2 || transfer_size_count>=ts_eff-2) begin
                        m_axis_tlast <= 1;
                    end
                end    
            end
            IDLE: begin
                rd_en<=0;
                wr_en<=0;
                m_axis_tlast <= 0;
                ifg_count<= ifg_count+1;
                slot_processed <=0;
                if (side_info_enabled) begin
                    if (side_info_completed) begin
                       ifg_count<= 0;
                       state_w <= SEND;
                       rd_en<=1;
                       wr_en<=1;
                    end
                end
                else begin
                   if(ifg_count>=ifg-1) begin
                       ifg_count<= 0;
                       state_w <= SEND;
                       rd_en<=1;
                       wr_en<=1;
                   end
                end
            end

            default: begin
                state_w <= WAIT_TRIGGER;
            end
        endcase
    end
end

always @ (state_w) begin
    case (state_w)
        2'b00 : transfer_init<= 0;
        2'b01 : transfer_init<= 1;
        2'b10 : transfer_init<= 1;
        default :transfer_init<= 0;
    endcase
end

reg signal_in_d;  // Delayed version of input signal

// Process for detecting rising edge
always @(posedge clk) begin
    if(!arst) begin
        signal_in_d <= 0;
        reset_out   <= 1;
    end else begin
        reset_out   <= 0;
        //signal_in_d <= transfer_init;  // Delay the input signal
        // Generate pulse if rising edge is detected
        //reset_out <= (transfer_init & ~signal_in_d);
    end
end


axis_register_slice_0 axis_register_slice_0(
  .aclk(clk),
  .aresetn(arst),
  .s_axis_tvalid(s_axis_tvalid),
  .s_axis_tready(s_tready_reg),
  .s_axis_tdata(s_axis_tdata),
  
  .m_axis_tvalid(m_tvalid_reg),
  .m_axis_tready(m_tready_reg),
  .m_axis_tdata(m_axis_tdata)
);

//ila_0 ila_0(
//    .clk (clk),
//    .probe0 (m_axis_tvalid),
//    .probe1 (s_axis_tready),
//    .probe2 (m_axis_tready),
//    .probe3 (s_axis_tvalid),
//    .probe4 (state_w),
//    .probe5 (mss_count),
//    .probe6 (ifg_count),
//    .probe7 (transfer_size_count),
//    .probe8 (m_axis_tdata),
//    .probe9 (m_axis_tlast),
//    .probe10 (trigger),
//    .probe11 (trigger_r),
//    .probe12 (trigger_rising_edge)
    
//);

      
    
endmodule