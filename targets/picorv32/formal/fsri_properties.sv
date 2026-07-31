// bounded check for direct FSRI arithmetic and disabled decoding
module fsri_properties (
    input logic clk
);

logic resetn = 1'b0;
logic [2:0] cycle = 3'b0;
(* anyconst *) logic [31:0] request_insn;
(* anyconst *) logic [31:0] rs1;
(* anyconst *) logic [31:0] rs2;
logic pcpi_valid;
logic pcpi_wr;
logic [31:0] pcpi_rd;
logic pcpi_wait;
logic pcpi_ready;
logic disabled_wr;
logic [31:0] disabled_rd;
logic disabled_wait;
logic disabled_ready;
logic [63:0] expected;

always_comb
begin
    pcpi_valid = resetn && cycle == 3'd1;
    expected = {rs2, rs1} >> request_insn[29:25];
end

pqc_pcpi_mlkem #(.ENABLE_FSRI(1'b1)) dut (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid),
    .pcpi_insn(request_insn), .pcpi_rs1(rs1), .pcpi_rs2(rs2),
    .pcpi_wr(pcpi_wr), .pcpi_rd(pcpi_rd), .pcpi_wait(pcpi_wait),
    .pcpi_ready(pcpi_ready)
);

pqc_pcpi_mlkem disabled (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid),
    .pcpi_insn(request_insn), .pcpi_rs1(rs1), .pcpi_rs2(rs2),
    .pcpi_wr(disabled_wr), .pcpi_rd(disabled_rd),
    .pcpi_wait(disabled_wait), .pcpi_ready(disabled_ready)
);

always_ff @(posedge clk)
begin
    assume((request_insn & 32'hc000_707f) == 32'h0000_200b);

    cycle <= cycle + 1'b1;
    resetn <= 1'b1;

    if (cycle == 3'd1)
    begin
        assert(!pcpi_wait);
        assert(pcpi_ready);
        assert(pcpi_wr);
        assert(pcpi_rd == expected[31:0]);
        assert(!disabled_wait);
        assert(!disabled_ready);
        assert(!disabled_wr);
    end
    if (cycle == 3'd2)
    begin
        assert(!pcpi_wait);
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end
end

endmodule
