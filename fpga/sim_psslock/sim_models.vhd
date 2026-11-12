-- Claude 2026-11-12: behavioural stand-ins used only by tb_psslock (the real design binds generated FIFO/BRAM/ILA IP).
library IEEE; use IEEE.STD_LOGIC_1164.ALL; use IEEE.NUMERIC_STD.ALL;
entity fwft_fifo_model is
generic (AW : integer := 11);
port (clk, srst : in std_logic; din : in std_logic_vector(31 downto 0); wr_en, rd_en : in std_logic;
      dout : out std_logic_vector(31 downto 0); full, empty : out std_logic);
end fwft_fifo_model;
architecture sim of fwft_fifo_model is
    type mem_t is array (0 to 2**AW-1) of std_logic_vector(31 downto 0);
    signal mem : mem_t := (others => (others => '0'));
    signal wp, rp : unsigned(AW downto 0) := (others => '0');
    signal e, f : std_logic;
begin
    e <= '1' when wp = rp else '0';
    f <= '1' when (wp(AW) /= rp(AW)) and (wp(AW-1 downto 0) = rp(AW-1 downto 0)) else '0';
    empty <= e; full <= f; dout <= mem(to_integer(rp(AW-1 downto 0)));
    process(clk) begin
        if rising_edge(clk) then
            if srst = '1' then wp <= (others => '0'); rp <= (others => '0');
            else
                if wr_en = '1' and f = '0' then mem(to_integer(wp(AW-1 downto 0))) <= din; wp <= wp + 1; end if;
                if rd_en = '1' and e = '0' then rp <= rp + 1; end if;
            end if;
        end if;
    end process;
end sim;

library IEEE; use IEEE.STD_LOGIC_1164.ALL;
entity FIFO_SYM_OFFSET is
port (clk, srst : in std_logic; din : in std_logic_vector(31 downto 0); wr_en, rd_en : in std_logic;
      dout : out std_logic_vector(31 downto 0); full, empty, wr_rst_busy, rd_rst_busy : out std_logic);
end FIFO_SYM_OFFSET;
architecture sim of FIFO_SYM_OFFSET is begin
    u: entity work.fwft_fifo_model generic map (AW => 8) port map (clk => clk, srst => srst, din => din, wr_en => wr_en, rd_en => rd_en, dout => dout, full => full, empty => empty);
    wr_rst_busy <= '0'; rd_rst_busy <= '0';
end sim;

library IEEE; use IEEE.STD_LOGIC_1164.ALL;
entity FFT_SHIFT_FIFO is
port (clk, srst : in std_logic; din : in std_logic_vector(31 downto 0); wr_en, rd_en : in std_logic;
      dout : out std_logic_vector(31 downto 0); full, empty, wr_rst_busy, rd_rst_busy : out std_logic);
end FFT_SHIFT_FIFO;
architecture sim of FFT_SHIFT_FIFO is begin
    u: entity work.fwft_fifo_model generic map (AW => 11) port map (clk => clk, srst => srst, din => din, wr_en => wr_en, rd_en => rd_en, dout => dout, full => full, empty => empty);
    wr_rst_busy <= '0'; rd_rst_busy <= '0';
end sim;

library IEEE; use IEEE.STD_LOGIC_1164.ALL;
entity ila_0_ofdm is port (clk : in std_logic; probe0, probe1 : in std_logic_vector(0 downto 0); probe2 : in std_logic_vector(11 downto 0); probe3 : in std_logic_vector(4 downto 0)); end ila_0_ofdm;
architecture sim of ila_0_ofdm is begin end sim;
library IEEE; use IEEE.STD_LOGIC_1164.ALL;
entity ila_SSB is port (clk : in std_logic; probe0, probe1, probe2 : in std_logic_vector(0 downto 0)); end ila_SSB;
architecture sim of ila_SSB is begin end sim;
