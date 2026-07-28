module pqc_pcpi_observe #(
    parameter ENABLE_FQMUL = 1'b0,
    parameter ENABLE_RED32 = 1'b0,
    parameter ENABLE_FSRI = 1'b0
) (
    input logic clk,
    input logic resetn,
    input logic pcpi_valid,
    input logic [31:0] pcpi_insn,
    input logic [31:0] pcpi_rs1,
    input logic [31:0] pcpi_rs2,
    output logic pcpi_wr,
    output logic [31:0] pcpi_rd,
    output logic pcpi_wait,
    output logic pcpi_ready,
    output logic [2:0] state,
    output logic [31:0] last_rs1,
    output logic [31:0] last_rs2,
    output logic [31:0] product_value,
    output logic [15:0] inverse_value,
    output logic [31:0] modulus_value,
    output logic [31:0] response_value,
    output logic [32:0] multiply_left,
    output logic [32:0] multiply_right,
    output logic [65:0] multiply_result,
    output logic [32:0] numerator
);
pqc_pcpi_mlkem #(
    .ENABLE_FQMUL(ENABLE_FQMUL),
    .ENABLE_RED32(ENABLE_RED32),
    .ENABLE_FSRI(ENABLE_FSRI)
) dut (
    .clk(clk), .resetn(resetn), .pcpi_valid(pcpi_valid),
    .pcpi_insn(pcpi_insn), .pcpi_rs1(pcpi_rs1), .pcpi_rs2(pcpi_rs2),
    .pcpi_wr(pcpi_wr), .pcpi_rd(pcpi_rd),
    .pcpi_wait(pcpi_wait), .pcpi_ready(pcpi_ready)
);
assign state = dut.state;
assign last_rs1 = dut.last_rs1;
assign last_rs2 = dut.last_rs2;
assign product_value = dut.product_value;
assign inverse_value = dut.inverse_value;
assign modulus_value = dut.modulus_value;
assign response_value = dut.response_value;
assign multiply_left = dut.multiply_left;
assign multiply_right = dut.multiply_right;
assign multiply_result = dut.multiply_result;
assign numerator = dut.numerator;
endmodule
