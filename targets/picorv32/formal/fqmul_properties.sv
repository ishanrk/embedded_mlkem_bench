module fqmul_properties (
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
logic [2:0] age = 3'b0;
logic [31:0] held_insn = 32'b0;
logic [31:0] held_rs1 = 32'b0;
logic [31:0] held_rs2 = 32'b0;
logic [212:0] dut_state;
logic signed [32:0] multiply_left;
logic signed [32:0] multiply_right;
logic signed [65:0] multiply_result;
logic signed [32:0] formal_numerator;
logic signed [31:0] formal_fqmul_result;
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

wire fqmul_decode = pcpi_valid &&
                    (pcpi_insn & 32'hfe00_707f) == 32'h0000_000b;
wire m_decode = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
                pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b0;
wire divider_decode = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
                      pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b1;

pqc_pcpi_mlkem #(
    .ENABLE_FQMUL(1'b1)
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
    .formal_fqmul_result(formal_fqmul_result)
);

always_ff @(posedge clk)
begin
    past_valid <= 1'b1;
    if (!resetn)
    begin
        pending <= 1'b0;
        age <= 3'b0;
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
            if (age == 3)
            begin
                assert(dut_state[31:0] == $past(multiply_result[31:0]));
                assert(pcpi_ready);
                assert(pcpi_wr);
                assert(!pcpi_wait);
                assert(formal_numerator == expected_numerator);
                assert(formal_fqmul_result ==
                       $signed({{15{formal_numerator[32]}}, formal_numerator[32:16]}));
                assert(pcpi_rd == formal_fqmul_result);
                pending <= 1'b0;
            end
            else
            begin
                assert(!pcpi_ready);
                assert(pcpi_wait);
                if (age == 0)
                begin
                    assert(multiply_left == $signed({{17{held_rs1[15]}}, held_rs1[15:0]}));
                    assert(multiply_right == $signed({{17{held_rs2[15]}}, held_rs2[15:0]}));
                end
                if (age == 1)
                begin
                    assert(dut_state[79:48] == $past(multiply_result[31:0]));
                    assert(multiply_left == $signed({17'b0, dut_state[63:48]}));
                    assert(multiply_right == 33'sd62209);
                end
                if (age == 2)
                begin
                    assert(dut_state[47:32] == $past(multiply_result[15:0]));
                    assert(multiply_left == $signed({{17{dut_state[47]}}, dut_state[47:32]}));
                    assert(multiply_right == 33'sd3329);
                end
                age <= age + 1'b1;
            end
        end
        else if (fqmul_decode && pcpi_wait && !pcpi_ready)
        begin
            assert(pcpi_wait);
            assert(!pcpi_ready);
            pending <= 1'b1;
            age <= 3'b0;
            held_insn <= pcpi_insn;
            held_rs1 <= pcpi_rs1;
            held_rs2 <= pcpi_rs2;
        end

        if (back_to_back)
        begin
            assert(dut_state[212:210] == 3'd1);
            assert(dut_state[175:144] == next_rs1);
            assert(dut_state[143:112] == next_rs2);
            assert(dut_state[207:176] == next_insn);
            back_to_back <= 1'b0;
        end
        if (past_valid && $past(pcpi_ready && fqmul_decode) && fqmul_decode &&
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

        assert(!(fqmul_decode && m_decode));
        assert(!(fqmul_decode && divider_decode));
        if (pcpi_valid && !fqmul_decode && !m_decode)
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

module fqmul_m_noninterference (
    input logic clk
);

logic resetn = 1'b0;
(* anyseq *) logic pcpi_valid;
(* anyseq *) logic [31:0] pcpi_insn;
(* anyseq *) logic [31:0] pcpi_rs1;
(* anyseq *) logic [31:0] pcpi_rs2;
logic pcpi_wr;
logic [31:0] pcpi_rd;
logic pcpi_wait;
logic pcpi_ready;
logic pending = 1'b0;
logic [31:0] held_insn = 32'b0;
logic [31:0] held_rs1 = 32'b0;
logic [31:0] held_rs2 = 32'b0;
logic [212:0] dut_state;
logic signed [32:0] multiply_left;
logic signed [32:0] multiply_right;
logic signed [65:0] multiply_result;
logic signed [32:0] expected_left;
logic signed [32:0] expected_right;

wire m_decode = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
                pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b0;

always_comb
begin
    expected_left = $signed({1'b0, held_rs1});
    expected_right = $signed({1'b0, held_rs2});
    if (held_insn[13:12] == 2'b01 || held_insn[13:12] == 2'b10)
    begin
        expected_left = $signed({held_rs1[31], held_rs1});
    end
    if (held_insn[13:12] == 2'b01)
    begin
        expected_right = $signed({held_rs2[31], held_rs2});
    end
end

pqc_pcpi_mlkem #(.ENABLE_FQMUL(1'b1)) dut (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid), .pcpi_insn(pcpi_insn),
    .pcpi_rs1(pcpi_rs1), .pcpi_rs2(pcpi_rs2), .pcpi_wr(pcpi_wr),
    .pcpi_rd(pcpi_rd), .pcpi_wait(pcpi_wait), .pcpi_ready(pcpi_ready),
    .formal_state(dut_state), .formal_multiply_left(multiply_left),
    .formal_multiply_right(multiply_right), .formal_multiply_result(multiply_result)
);

always_ff @(posedge clk)
begin
    resetn <= 1'b1;
    if (!resetn)
    begin
        pending <= 1'b0;
    end
    else if (pending)
    begin
        assume(pcpi_valid);
        assume(pcpi_insn == held_insn);
        assume(pcpi_rs1 == held_rs1);
        assume(pcpi_rs2 == held_rs2);
        if (dut_state[212:210] == 3'd1)
        begin
            assert(multiply_left == expected_left);
            assert(multiply_right == expected_right);
            assert(pcpi_wait);
            assert(!pcpi_ready);
        end
        else
        begin
            if (held_insn[14:12] == 3'b000)
            begin
                assert(dut_state[111:80] == $past(multiply_result[31:0]));
            end
            else
            begin
                assert(dut_state[111:80] == $past(multiply_result[63:32]));
            end
            assert(pcpi_ready);
            assert(pcpi_wr);
            assert(!pcpi_wait);
            assert(pcpi_rd == dut_state[111:80]);
            pending <= 1'b0;
        end
    end
    else if (m_decode && pcpi_wait && !pcpi_ready)
    begin
        pending <= 1'b1;
        held_insn <= pcpi_insn;
        held_rs1 <= pcpi_rs1;
        held_rs2 <= pcpi_rs2;
    end
end

endmodule
