// bounded check for one arbitrary RED32 request and one unrelated request
module red32_properties (
    input logic clk
);

logic resetn = 1'b0;
logic [3:0] cycle = 4'b0;
(* anyconst *) logic [31:0] request_insn;
(* anyconst *) logic [31:0] bad_insn;
(* anyconst *) logic [31:0] value;
(* anyconst *) logic [31:0] ignored;
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
logic [15:0] reference_inverse_bits;
logic signed [31:0] reference_inverse;
logic signed [31:0] reference_modulus;
logic signed [32:0] reference_numerator;
logic signed [31:0] reference_result;

wire bad_multiply = bad_insn[6:0] == 7'b0110011 &&
                    bad_insn[31:25] == 7'b0000001 &&
                    bad_insn[14] == 1'b0;

always_comb
begin
    pcpi_valid = resetn && cycle >= 4'd1 && cycle <= 4'd5;
    pcpi_insn = cycle == 4'd5 ? bad_insn : request_insn;
    reference_inverse_bits = value[15:0] * 16'd62209;
    reference_inverse =
        $signed({{16{reference_inverse_bits[15]}}, reference_inverse_bits});
    reference_modulus = reference_inverse * 32'sd3329;
    reference_numerator = $signed({value[31], value}) -
                          $signed({reference_modulus[31], reference_modulus});
    reference_result =
        $signed({{15{reference_numerator[32]}}, reference_numerator[32:16]});
end

pqc_pcpi_mlkem #(.ENABLE_RED32(1'b1)) dut (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid),
    .pcpi_insn(pcpi_insn), .pcpi_rs1(value), .pcpi_rs2(ignored),
    .pcpi_wr(pcpi_wr), .pcpi_rd(pcpi_rd), .pcpi_wait(pcpi_wait),
    .pcpi_ready(pcpi_ready)
);

pqc_pcpi_mlkem disabled (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid),
    .pcpi_insn(request_insn), .pcpi_rs1(value), .pcpi_rs2(ignored),
    .pcpi_wr(disabled_wr), .pcpi_rd(disabled_rd),
    .pcpi_wait(disabled_wait), .pcpi_ready(disabled_ready)
);

always_ff @(posedge clk)
begin
    assume((request_insn & 32'hfe00_707f) == 32'h0000_100b);
    assume((bad_insn & 32'hfe00_707f) != 32'h0000_100b);
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
