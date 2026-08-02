module dot2x_properties (
    input logic clk
);

logic resetn = 1'b0;
logic [3:0] cycle = 4'b0;
(* anyconst *) logic [31:0] request_insn;
(* anyconst *) logic [31:0] bad_insn;
(* anyconst *) logic [31:0] rs1;
(* anyconst *) logic [31:0] rs2;
logic pcpi_valid;
logic [31:0] pcpi_insn;
logic pcpi_wr;
logic [31:0] pcpi_rd;
logic pcpi_wait;
logic pcpi_ready;
logic disabled_wr;
logic [31:0] disabled_rd;
logic disabled_wait;
logic disabled_ready;
logic signed [32:0] reference_a0;
logic signed [32:0] reference_a1;
logic signed [32:0] reference_b0;
logic signed [32:0] reference_b1;
logic signed [65:0] reference_product0;
logic signed [65:0] reference_product1;
logic [31:0] reference_result;

wire bad_multiply = bad_insn[6:0] == 7'b0110011 &&
                    bad_insn[31:25] == 7'b0000001 &&
                    bad_insn[14] == 1'b0;

always_comb
begin
    pcpi_valid = resetn && cycle >= 4'd1 && cycle <= 4'd5;
    pcpi_insn = cycle == 4'd5 ? bad_insn : request_insn;
    reference_a0 = $signed({{17{rs1[15]}}, rs1[15:0]});
    reference_a1 = $signed({{17{rs1[31]}}, rs1[31:16]});
    reference_b0 = $signed({{17{rs2[15]}}, rs2[15:0]});
    reference_b1 = $signed({{17{rs2[31]}}, rs2[31:16]});
    reference_product0 = reference_a0 * reference_b1;
    reference_product1 = reference_a1 * reference_b0;
    reference_result = reference_product0[31:0] + reference_product1[31:0];
end

pqc_pcpi_mlkem #(.ENABLE_DOT2X(1'b1)) dut (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid),
    .pcpi_insn(pcpi_insn), .pcpi_rs1(rs1), .pcpi_rs2(rs2),
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
    assume((request_insn & 32'hfe00_707f) == 32'h0000_300b);
    assume((bad_insn & 32'hfe00_707f) != 32'h0000_300b);
    assume(!bad_multiply);

    cycle <= cycle + 1'b1;
    resetn <= 1'b1;

    if (cycle >= 4'd1 && cycle <= 4'd3)
    begin
        assert(pcpi_wait);
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end
    if (cycle == 4'd4)
    begin
        assert(!pcpi_wait);
        assert(pcpi_ready);
        assert(pcpi_wr);
        assert(pcpi_rd == reference_result);
    end
    if (cycle == 4'd5)
    begin
        assert(!pcpi_wait);
        assert(!pcpi_ready);
        assert(!pcpi_wr);
    end
    if (cycle >= 4'd1 && cycle <= 4'd4)
    begin
        assert(!disabled_wait);
        assert(!disabled_ready);
        assert(!disabled_wr);
    end
end

endmodule
