library IEEE;
use IEEE.STD_LOGIC_1164.ALL;
use IEEE.NUMERIC_STD.ALL;

-- Behavioral dual-port RAM model used only by the focused packet_sync XSim
-- testbench.  The packaged IP still binds the generated PACKET_SYNC_MEM IP.
entity PACKET_SYNC_MEM is
port (
    clka  : in  std_logic;
    ena   : in  std_logic;
    wea   : in  std_logic_vector(0 downto 0);
    addra : in  std_logic_vector(13 downto 0);
    dina  : in  std_logic_vector(63 downto 0);
    clkb  : in  std_logic;
    enb   : in  std_logic;
    addrb : in  std_logic_vector(13 downto 0);
    doutb : out std_logic_vector(63 downto 0)
);
end PACKET_SYNC_MEM;

architecture sim of PACKET_SYNC_MEM is
    type ram_t is array (0 to 2**14 - 1) of std_logic_vector(63 downto 0);
    signal ram : ram_t := (others => (others => '0'));
begin
    process(clka)
    begin
        if rising_edge(clka) then
            if ena = '1' and wea(0) = '1' then
                ram(to_integer(unsigned(addra))) <= dina;
            end if;
        end if;
    end process;

    process(clkb)
    begin
        if rising_edge(clkb) then
            if enb = '1' then
                doutb <= ram(to_integer(unsigned(addrb)));
            end if;
        end if;
    end process;
end sim;
