--Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
--Copyright 2022-2024 Advanced Micro Devices, Inc. All Rights Reserved.
----------------------------------------------------------------------------------
--Tool Version: Vivado v.2024.1.2 (win64) Build 5164865 Thu Sep  5 14:37:11 MDT 2024
--Date        : Thu Nov 12 18:08:52 2026
--Host        : DESKTOP-L92A5CH running 64-bit major release  (build 9200)
--Command     : generate_target design_1_wrapper.bd
--Design      : design_1_wrapper
--Purpose     : IP block netlist
----------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
library UNISIM;
use UNISIM.VCOMPONENTS.ALL;
entity design_1_wrapper is
  port (
    GPIO_0_tri_o : out STD_LOGIC_VECTOR ( 1 downto 0 );
    adc2_clk_0_clk_n : in STD_LOGIC;
    adc2_clk_0_clk_p : in STD_LOGIC;
    dac0_clk_0_clk_n : in STD_LOGIC;
    dac0_clk_0_clk_p : in STD_LOGIC;
    dip_switches_4bits_tri_i : in STD_LOGIC_VECTOR ( 3 downto 0 );
    pl_clk_clk_n : in STD_LOGIC_VECTOR ( 0 to 0 );
    pl_clk_clk_p : in STD_LOGIC_VECTOR ( 0 to 0 );
    pl_sysref_clk_n : in STD_LOGIC_VECTOR ( 0 to 0 );
    pl_sysref_clk_p : in STD_LOGIC_VECTOR ( 0 to 0 );
    sysref_in_0_diff_n : in STD_LOGIC;
    sysref_in_0_diff_p : in STD_LOGIC;
    vin2_01_0_v_n : in STD_LOGIC;
    vin2_01_0_v_p : in STD_LOGIC;
    vin2_23_0_v_n : in STD_LOGIC;
    vin2_23_0_v_p : in STD_LOGIC;
    vout00_0_v_n : out STD_LOGIC;
    vout00_0_v_p : out STD_LOGIC;
    vout02_0_v_n : out STD_LOGIC;
    vout02_0_v_p : out STD_LOGIC
  );
end design_1_wrapper;

architecture STRUCTURE of design_1_wrapper is
  component design_1 is
  port (
    GPIO_0_tri_o : out STD_LOGIC_VECTOR ( 1 downto 0 );
    adc2_clk_0_clk_n : in STD_LOGIC;
    adc2_clk_0_clk_p : in STD_LOGIC;
    dac0_clk_0_clk_n : in STD_LOGIC;
    dac0_clk_0_clk_p : in STD_LOGIC;
    sysref_in_0_diff_n : in STD_LOGIC;
    sysref_in_0_diff_p : in STD_LOGIC;
    vin2_01_0_v_n : in STD_LOGIC;
    vin2_01_0_v_p : in STD_LOGIC;
    vin2_23_0_v_n : in STD_LOGIC;
    vin2_23_0_v_p : in STD_LOGIC;
    vout00_0_v_n : out STD_LOGIC;
    vout00_0_v_p : out STD_LOGIC;
    vout02_0_v_n : out STD_LOGIC;
    vout02_0_v_p : out STD_LOGIC;
    dip_switches_4bits_tri_i : in STD_LOGIC_VECTOR ( 3 downto 0 );
    pl_clk_clk_p : in STD_LOGIC_VECTOR ( 0 to 0 );
    pl_clk_clk_n : in STD_LOGIC_VECTOR ( 0 to 0 );
    pl_sysref_clk_p : in STD_LOGIC_VECTOR ( 0 to 0 );
    pl_sysref_clk_n : in STD_LOGIC_VECTOR ( 0 to 0 )
  );
  end component design_1;
begin
design_1_i: component design_1
     port map (
      GPIO_0_tri_o(1 downto 0) => GPIO_0_tri_o(1 downto 0),
      adc2_clk_0_clk_n => adc2_clk_0_clk_n,
      adc2_clk_0_clk_p => adc2_clk_0_clk_p,
      dac0_clk_0_clk_n => dac0_clk_0_clk_n,
      dac0_clk_0_clk_p => dac0_clk_0_clk_p,
      dip_switches_4bits_tri_i(3 downto 0) => dip_switches_4bits_tri_i(3 downto 0),
      pl_clk_clk_n(0) => pl_clk_clk_n(0),
      pl_clk_clk_p(0) => pl_clk_clk_p(0),
      pl_sysref_clk_n(0) => pl_sysref_clk_n(0),
      pl_sysref_clk_p(0) => pl_sysref_clk_p(0),
      sysref_in_0_diff_n => sysref_in_0_diff_n,
      sysref_in_0_diff_p => sysref_in_0_diff_p,
      vin2_01_0_v_n => vin2_01_0_v_n,
      vin2_01_0_v_p => vin2_01_0_v_p,
      vin2_23_0_v_n => vin2_23_0_v_n,
      vin2_23_0_v_p => vin2_23_0_v_p,
      vout00_0_v_n => vout00_0_v_n,
      vout00_0_v_p => vout00_0_v_p,
      vout02_0_v_n => vout02_0_v_n,
      vout02_0_v_p => vout02_0_v_p
    );
end STRUCTURE;
