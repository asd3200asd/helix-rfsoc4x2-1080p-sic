`timescale 1ns/1ps
module axis_register_slice_0(input aclk, input aresetn, input s_axis_tvalid, output s_axis_tready, input [63:0] s_axis_tdata,
  output m_axis_tvalid, input m_axis_tready, output [63:0] m_axis_tdata);
  assign m_axis_tvalid = s_axis_tvalid; assign s_axis_tready = m_axis_tready; assign m_axis_tdata = s_axis_tdata;
endmodule