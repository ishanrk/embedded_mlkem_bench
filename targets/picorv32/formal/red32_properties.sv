module red32_properties (
    input logic clk
);

(* anyseq *) logic resetn;
(* anyseq *) logic pcpi_valid;
(* anyseq *) logic [31:0] pcpi_insn;
(* anyseq *) logic [31:0] pcpi_rs1;
(* anyseq *) logic [31:0] pcpi_rs2;
logic pcpi_wr;
logic [31:0] pcpi_rd;
logic pcpi_wait;
logic pcpi_ready;
logic past_valid = 1'b0;
logic pending = 1'b0;
logic [1:0] age = 2'b0;
logic [31:0] held_insn = 32'b0;
logic [31:0] held_rs1 = 32'b0;
logic [31:0] held_rs2 = 32'b0;
logic [212:0] dut_state;
logic signed [32:0] multiply_left;
logic signed [32:0] multiply_right;
logic signed [65:0] multiply_result;
logic signed [32:0] formal_numerator;
logic signed [31:0] formal_result;
logic signed [32:0] expected_numerator;
logic back_to_back = 1'b0;
logic [31:0] next_insn = 32'b0;
logic [31:0] next_rs1 = 32'b0;
logic [31:0] next_rs2 = 32'b0;

initial assume(!resetn);

always_comb
begin
    expected_numerator = $signed({dut_state[79], dut_state[79:48]}) -
                         $signed({dut_state[31], dut_state[31:0]});
end

wire red32_decode = pcpi_valid &&
                    (pcpi_insn & 32'hfe00_707f) == 32'h0000_100b;
wire fqmul_decode = pcpi_valid &&
                    (pcpi_insn & 32'hfe00_707f) == 32'h0000_000b;
wire m_decode = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
                pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b0;
wire divider_decode = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
                      pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b1;

pqc_pcpi_mlkem #(
    .ENABLE_FQMUL(1'b1),
    .ENABLE_RED32(1'b1)
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
    .pcpi_ready(pcpi_ready),
    .formal_state(dut_state),
    .formal_multiply_left(multiply_left),
    .formal_multiply_right(multiply_right),
    .formal_multiply_result(multiply_result),
    .formal_numerator(formal_numerator),
    .formal_fqmul_result(formal_result)
);

always_ff @(posedge clk)
begin
    past_valid <= 1'b1;
    if (!resetn)
    begin
        pending <= 1'b0;
        age <= 2'b0;
        back_to_back <= 1'b0;
    end
    else
    begin
        if (pending)
        begin
            assume(pcpi_valid);
            assume(pcpi_insn == held_insn);
            assume(pcpi_rs1 == held_rs1);
            assume(pcpi_rs2 == held_rs2);
            if (age == 2)
            begin
                assert(dut_state[212:210] == 3'd4);
                assert(dut_state[31:0] == $past(multiply_result[31:0]));
                assert(dut_state[79:48] == held_rs1);
                assert(pcpi_ready);
                assert(pcpi_wr);
                assert(!pcpi_wait);
                assert(formal_numerator == expected_numerator);
                assert(formal_result ==
                       $signed({{15{formal_numerator[32]}}, formal_numerator[32:16]}));
                assert(pcpi_rd == formal_result);
                assert($signed(formal_result) >= -32'sd34432);
                assert($signed(formal_result) <= 32'sd34432);
                pending <= 1'b0;
            end
            else
            begin
                assert(!pcpi_ready);
                assert(pcpi_wait);
                if (age == 0)
                begin
                    assert(dut_state[212:210] == 3'd2);
                    assert(dut_state[79:48] == held_rs1);
                    assert(multiply_left == $signed({17'b0, held_rs1[15:0]}));
                    assert(multiply_right == 33'sd62209);
                end
                if (age == 1)
                begin
                    assert(dut_state[212:210] == 3'd3);
                    assert(dut_state[47:32] == $past(multiply_result[15:0]));
                    assert(multiply_left == $signed({{17{dut_state[47]}}, dut_state[47:32]}));
                    assert(multiply_right == 33'sd3329);
                end
                age <= age + 1'b1;
            end
        end
        else if (red32_decode && pcpi_wait && !pcpi_ready)
        begin
            assert(pcpi_wait);
            assert(!pcpi_ready);
            pending <= 1'b1;
            age <= 2'b0;
            held_insn <= pcpi_insn;
            held_rs1 <= pcpi_rs1;
            held_rs2 <= pcpi_rs2;
        end

        if (back_to_back)
        begin
            assert(dut_state[212:210] == 3'd2);
            assert(dut_state[175:144] == next_rs1);
            assert(dut_state[143:112] == next_rs2);
            assert(dut_state[207:176] == next_insn);
            assert(dut_state[79:48] == next_rs1);
            back_to_back <= 1'b0;
        end
        if (past_valid && $past(pcpi_ready && red32_decode) && red32_decode &&
            (pcpi_insn != $past(pcpi_insn) || pcpi_rs1 != $past(pcpi_rs1) ||
             pcpi_rs2 != $past(pcpi_rs2)))
        begin
            assert(pcpi_wait);
            assert(!pcpi_ready);
            back_to_back <= 1'b1;
            next_insn <= pcpi_insn;
            next_rs1 <= pcpi_rs1;
            next_rs2 <= pcpi_rs2;
        end

        assert(!(red32_decode && fqmul_decode));
        assert(!(red32_decode && m_decode));
        assert(!(red32_decode && divider_decode));
        if (!pending && pcpi_valid && !red32_decode && !fqmul_decode && !m_decode)
        begin
            assert(!pcpi_ready);
            assert(!pcpi_wr);
        end
        if (past_valid && !$past(resetn))
        begin
            assert(dut_state[212:210] == 3'd0);
            assert(!pcpi_ready);
            assert(!pcpi_wr);
        end
    end
end

endmodule

module red32_noninterference (
    input logic clk
);

(* anyseq *) logic resetn;
(* anyseq *) logic pcpi_valid;
(* anyseq *) logic [31:0] pcpi_insn;
(* anyseq *) logic [31:0] pcpi_rs1;
(* anyseq *) logic [31:0] pcpi_rs2;
logic base_wr;
logic [31:0] base_rd;
logic base_wait;
logic base_ready;
logic extended_wr;
logic [31:0] extended_rd;
logic extended_wait;
logic extended_ready;
logic [212:0] base_state;
logic [212:0] extended_state;
logic past_valid = 1'b0;

initial assume(!resetn);

wire red32_decode = pcpi_valid &&
                    (pcpi_insn & 32'hfe00_707f) == 32'h0000_100b;

pqc_pcpi_mlkem #(.ENABLE_FQMUL(1'b1), .ENABLE_RED32(1'b0)) base (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid), .pcpi_insn(pcpi_insn),
    .pcpi_rs1(pcpi_rs1), .pcpi_rs2(pcpi_rs2), .pcpi_wr(base_wr), .pcpi_rd(base_rd),
    .pcpi_wait(base_wait), .pcpi_ready(base_ready), .formal_state(base_state)
);

pqc_pcpi_mlkem #(.ENABLE_FQMUL(1'b1), .ENABLE_RED32(1'b1)) extended (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid), .pcpi_insn(pcpi_insn),
    .pcpi_rs1(pcpi_rs1), .pcpi_rs2(pcpi_rs2), .pcpi_wr(extended_wr),
    .pcpi_rd(extended_rd), .pcpi_wait(extended_wait), .pcpi_ready(extended_ready),
    .formal_state(extended_state)
);

always_ff @(posedge clk)
begin
    assume(!red32_decode);
    if (past_valid)
    begin
        assert(base_wr == extended_wr);
        assert(base_rd == extended_rd);
        assert(base_wait == extended_wait);
        assert(base_ready == extended_ready);
        assert(base_state == extended_state);
    end
    past_valid <= 1'b1;
end

endmodule
