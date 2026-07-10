module fsri_properties (
    input logic clk
);

logic resetn = 1'b0;
logic [3:0] cycle = 4'b0;
(* anyconst *) logic cancel;
(* anyconst *) logic [31:0] insn;
(* anyconst *) logic [31:0] bad_insn;
(* anyconst *) logic [31:0] rs1;
(* anyconst *) logic [31:0] rs2;
logic pcpi_valid;
logic [31:0] pcpi_insn;
logic [31:0] pcpi_rs1;
logic [31:0] pcpi_rs2;
logic pcpi_wr;
logic [31:0] pcpi_rd;
logic pcpi_wait;
logic pcpi_ready;
logic [63:0] expected;

wire bad_m = bad_insn[6:0] == 7'b0110011 &&
             bad_insn[31:25] == 7'b0000001 && bad_insn[14] == 1'b0;

always_comb
begin
    pcpi_valid = 1'b0;
    pcpi_insn = insn;
    pcpi_rs1 = rs1;
    pcpi_rs2 = rs2;
    if (resetn && cycle >= 4'd1 && cycle <= 4'd4 && (!cancel || cycle <= 4'd2))
    begin
        pcpi_valid = 1'b1;
    end
    else if (cycle == 4'd6)
    begin
        pcpi_valid = 1'b1;
        pcpi_insn = bad_insn;
    end
    expected = {rs2, rs1} >> insn[29:25];
end

pqc_pcpi_mlkem #(
    .ENABLE_FSRI(1'b1)
) dut (
    .clk(clk),
    .resetn(resetn),
    .pcpi_valid(pcpi_valid),
    .pcpi_insn(pcpi_insn),
    .pcpi_rs1(pcpi_rs1),
    .pcpi_rs2(pcpi_rs2),
    .pcpi_wr(pcpi_wr),
    .pcpi_rd(pcpi_rd),
    .pcpi_wait(pcpi_wait),
    .pcpi_ready(pcpi_ready)
);

always_ff @(posedge clk)
begin
    assume((insn & 32'hc000_707f) == 32'h0000_200b);
    assume((bad_insn & 32'hc000_707f) != 32'h0000_200b);
    assume(!bad_m);

    cycle <= cycle + 1'b1;
    if (cycle == 4'd0)
    begin
        resetn <= 1'b1;
    end
    else if (cancel && cycle == 4'd2)
    begin
        resetn <= 1'b0;
    end
    else if (cycle == 4'd3)
    begin
        resetn <= 1'b1;
    end

    if (cycle == 4'd1 || cycle == 4'd2)
    begin
        assert(pcpi_wait);
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end

    if (!cancel && cycle == 4'd3)
    begin
        assert(pcpi_wait);
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end

    if (!cancel && cycle == 4'd4)
    begin
        assert(!pcpi_wait);
        assert(pcpi_ready);
        assert(pcpi_wr);
        assert(pcpi_rd == expected[31:0]);
    end

    if (cancel && (cycle == 4'd3 || cycle == 4'd4))
    begin
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end

    if (cycle == 4'd5)
    begin
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end

    if (cycle == 4'd6)
    begin
        assert(!pcpi_wait);
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end
end

endmodule
