module pqc_pcpi_mlkem (
    input  logic        clk,
    input  logic        resetn,
    input  logic        pcpi_valid,
    input  logic [31:0] pcpi_insn,
    input  logic [31:0] pcpi_rs1,
    input  logic [31:0] pcpi_rs2,
    output logic        pcpi_wr,
    output logic [31:0] pcpi_rd,
    output logic        pcpi_wait,
    output logic        pcpi_ready
);

logic claimed;
logic pending;
logic served;
logic changed;
logic response_ready;
logic [31:0] last_insn;
logic [31:0] last_rs1;
logic [31:0] last_rs2;
logic signed [32:0] left_operand;
logic signed [32:0] right_operand;
logic signed [65:0] product;
logic [31:0] result;

always_comb
begin
    claimed = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
              pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b0;
    changed = pcpi_insn != last_insn || pcpi_rs1 != last_rs1 || pcpi_rs2 != last_rs2;

    left_operand = $signed({1'b0, pcpi_rs1});
    right_operand = $signed({1'b0, pcpi_rs2});
    if (pcpi_insn[13:12] == 2'b01 || pcpi_insn[13:12] == 2'b10)
    begin
        left_operand = $signed({pcpi_rs1[31], pcpi_rs1});
    end
    if (pcpi_insn[13:12] == 2'b01)
    begin
        right_operand = $signed({pcpi_rs2[31], pcpi_rs2});
    end

    product = left_operand * right_operand;
    if (pcpi_insn[14:12] == 3'b000)
    begin
        result = product[31:0];
    end
    else
    begin
        result = product[63:32];
    end

    pcpi_ready = response_ready && claimed && !changed;
    pcpi_wr = pcpi_ready;
    pcpi_wait = claimed && !pcpi_ready && (pending || !served || changed);
end

always_ff @(posedge clk)
begin
    if (!resetn)
    begin
        pending <= 1'b0;
        served <= 1'b0;
        last_insn <= 32'b0;
        last_rs1 <= 32'b0;
        last_rs2 <= 32'b0;
        pcpi_rd <= 32'b0;
        response_ready <= 1'b0;
    end
    else
    begin
        response_ready <= 1'b0;

        if (!pcpi_valid)
        begin
            served <= 1'b0;
        end

        if (pending)
        begin
            pending <= 1'b0;
            response_ready <= 1'b1;
        end
        else if (claimed && (!served || changed))
        begin
            pending <= 1'b1;
            served <= 1'b1;
            last_insn <= pcpi_insn;
            last_rs1 <= pcpi_rs1;
            last_rs2 <= pcpi_rs2;
            pcpi_rd <= result;
        end
    end
end

endmodule
