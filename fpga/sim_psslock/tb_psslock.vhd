-- Claude 2026-11-12: does a second PSS detection at an unlucky time leave the receive framing permanently broken?
-- DUT: the nlsg packet_sync (slot framer inside SSB_TOP_v3) feeding OFDM_demod_TOP_v3, as in rx_blocks (CFO block omitted).
-- Regular detections arrive every P clocks; the input data is identical in every period, so every healthy slot must
-- produce identical output.  One extra detection is injected EXTRA clocks after the regular one of slot PERTURB_SLOT.
library IEEE; use IEEE.STD_LOGIC_1164.ALL; use IEEE.NUMERIC_STD.ALL; use work.tb_cfg.all;   -- tb_cfg: constant EXTRA (written per scenario by run.tcl)
entity tb_psslock is
generic (P : integer := 45000; NSLOT : integer := 8; PERTURB_SLOT : integer := 3);   -- P = 183 us, the on-air TB period at tx_ifg 180000
end tb_psslock;
architecture sim of tb_psslock is
    signal clk : std_logic := '0'; signal rstn : std_logic := '0'; signal done : std_logic := '0';
    signal n : integer := 0; signal sync : std_logic := '0';
    signal di, dq : std_logic_vector(31 downto 0) := (others => '0');
    signal m00, m01 : std_logic_vector(31 downto 0); signal m_valid, m_last, ps_sync : std_logic;
    signal o_re, o_im : std_logic_vector(31 downto 0); signal o_valid, o_last, o_trig, fifo_err : std_logic;
    function nxt(x : unsigned(31 downto 0)) return unsigned is variable l : unsigned(31 downto 0) := x; begin
        l := l xor shift_left(l, 13); l := l xor shift_right(l, 17); l := l xor shift_left(l, 5); return l; end function;
begin
    clk <= (not clk) and (not done) after 2 ns;
    stim: process(clk)
        variable l : unsigned(31 downto 0) := x"1234ABCD"; variable ph, k : integer;
    begin
        if rising_edge(clk) then
            if n < 16 then rstn <= '0'; else rstn <= '1'; end if;
            ph := n mod P; k := n / P;
            if ph = 0 then l := x"1234ABCD"; end if;
            l := nxt(l); di(15 downto 0)  <= std_logic_vector(resize(signed(l(11 downto 0)), 16));
            l := nxt(l); di(31 downto 16) <= std_logic_vector(resize(signed(l(11 downto 0)), 16));
            l := nxt(l); dq(15 downto 0)  <= std_logic_vector(resize(signed(l(11 downto 0)), 16));
            l := nxt(l); dq(31 downto 16) <= std_logic_vector(resize(signed(l(11 downto 0)), 16));
            sync <= '0';
            if n >= 16 and ph = 1000 and k < NSLOT then sync <= '1'; end if;
            if EXTRA >= 0 and n = PERTURB_SLOT * P + 1000 + EXTRA then sync <= '1'; report "EV EXTRA n=" & integer'image(n); end if;
            n <= n + 1;
            if n = (NSLOT + 1) * P then done <= '1'; report "EV DONE"; end if;
        end if;
    end process;

    ps: entity work.packet_sync
    port map (aclk => clk, aresetn => rstn, syncIn => sync, delayIn => std_logic_vector(to_unsigned(10447, 14)), syncOut => ps_sync,
              slotLength => std_logic_vector(to_unsigned(15472, 16)),
              s00_axis_tready => open, s00_axis_tdata => di, s00_axis_tkeep => "1111", s00_axis_tlast => '0', s00_axis_tvalid => '1',
              s01_axis_tready => open, s01_axis_tdata => dq, s01_axis_tkeep => "1111", s01_axis_tlast => '0', s01_axis_tvalid => '1',
              m00_axis_tvalid => m_valid, m00_axis_tdata => m00, m00_axis_tkeep => open, m00_axis_tlast => m_last, m00_axis_tready => '1',
              m01_axis_tvalid => open, m01_axis_tdata => m01, m01_axis_tkeep => open, m01_axis_tlast => open, m01_axis_tready => '1');

    ofdm: entity work.OFDM_demod_TOP_v3
    port map (clk => clk, rstn => rstn, GlobalStart => '1', dataIn_re => m00, dataIn_im => m01, validIn => m_valid,
              triggerIn => m00(0), triggerIn_TEMP => m00(0),
              cpLengths => std_logic_vector(to_unsigned(144, 9)) & std_logic_vector(to_unsigned(400, 9)),
              numOFDMSyms => std_logic_vector(to_unsigned(14, 7)), nullFirst => "000010011001", nullLast => "011101100110",
              countSCOut => open, countOFDMOut => open, fifoErrorOut => fifo_err, triggerOut => o_trig,
              dataOut_re => o_re, dataOut_im => o_im, lastOut => o_last, validOut => o_valid);

    mon: process(clk)
        variable cnt : integer := 0; variable h : unsigned(30 downto 0) := (others => '0'); variable t : unsigned(61 downto 0);
    begin
        if rising_edge(clk) and rstn = '1' then
            if m_valid = '1' and m00(0) = '1' then report "EV PSSTART n=" & integer'image(n) & " ph=" & integer'image(n mod P); end if;
            if m_valid = '1' and m_last = '1' then report "EV PSLAST n=" & integer'image(n) & " ph=" & integer'image(n mod P); end if;
            if fifo_err = '1' then report "EV FIFOERR n=" & integer'image(n); end if;
            if o_trig = '1' then report "EV OTRIG n=" & integer'image(n) & " ph=" & integer'image(n mod P); end if;
            if o_valid = '1' then
                cnt := cnt + 1; t := h * to_unsigned(33, 31); h := t(30 downto 0) + unsigned(o_re(14 downto 0)) + unsigned(o_im(30 downto 16));
            end if;
            if o_last = '1' then
                report "EV OLAST n=" & integer'image(n) & " ph=" & integer'image(n mod P) & " cnt=" & integer'image(cnt) & " hash=" & integer'image(to_integer(h));
                cnt := 0; h := (others => '0');
            end if;
        end if;
    end process;
end sim;
