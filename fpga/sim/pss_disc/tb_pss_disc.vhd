library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
-- Claude 2026-11-14: PSS0/PSS1 discrimination test.  Reports every rising edge of psscellidValidOut with the input sample index.
entity tb_pss_disc is generic (STIMN : integer := 0; SEL : integer := 0; DISC : integer := 1); end;
architecture sim of tb_pss_disc is
 function fname(k : integer) return string is begin if k = 1 then return "stim_p1.txt"; elsif k = 2 then return "stim_mx.txt"; elsif k = 3 then return "stim_cx.txt"; else return "stim_p0.txt"; end if; end function;
 signal clk : std_logic := '0'; signal reset : std_logic := '1';
 signal v : std_logic := '0'; signal i,q : std_logic_vector(15 downto 0) := (others=>'0');
 signal e : std_logic_vector(24 downto 0); signal ev : std_logic; signal id : std_logic_vector(1 downto 0); signal iv, iv_r : std_logic := '0';
 signal n : integer := 0; signal sel_s, disc_s : std_logic;
begin
 clk <= not clk after 2.0345 ns;
 sel_s <= '1' when SEL = 1 else '0'; disc_s <= '1' when DISC = 1 else '0';
 dut: entity work.PSS_RUNTIME_DETECTION_TOP port map(pss_select=>sel_s,pss_disc_en=>disc_s,clk=>clk,reset=>reset,clk_enable=>'1',dataIn_re=>i,dataIn_im=>q,validIn=>v,tempOut_re=>open,tempOut_im=>open,tempValidOut=>open,dataOut_TH=>e,validOut_TH=>ev,psscellidOut=>id,psscellidValidOut=>iv);
 mon: process(clk) begin if rising_edge(clk) then iv_r <= iv;
   if iv='1' and iv_r='0' and reset='0' then report "DET sample=" & integer'image(n) & " id=" & integer'image(to_integer(unsigned(id))); end if; end if; end process;
 stimuli: process
  file f : text open read_mode is fname(STIMN); variable ln : line; variable ai,aq,l : integer;
 begin
  wait for 150 ns; wait until falling_edge(clk);reset<='0';
  while not endfile(f) loop
   readline(f,ln);read(ln,ai);read(ln,aq);read(ln,l);
   i<=std_logic_vector(to_signed(ai,16));q<=std_logic_vector(to_signed(aq,16));v<='1'; n <= n + 1;
   wait until falling_edge(clk);v<='0';
   for j in 1 to 3 loop wait until falling_edge(clk);end loop;
  end loop;
  wait for 3000 ns;report "TB_DONE";std.env.finish;
 end process;
end architecture;