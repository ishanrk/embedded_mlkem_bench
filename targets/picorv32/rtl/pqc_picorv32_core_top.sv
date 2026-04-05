module pqc_picorv32_core_top #(
    parameter STOCK_MUL = 1'b0
) (
    input  logic        clk,
    input  logic        resetn,
    output logic        trap,
    output logic        mem_valid,
    output logic        mem_instr,
    input  logic        mem_ready,
    output logic [31:0] mem_addr,
    output logic [31:0] mem_wdata,
    output logic [3:0]  mem_wstrb,
    input  logic [31:0] mem_rdata
);

logic pcpi_valid;
logic [31:0] pcpi_insn;
logic [31:0] pcpi_rs1;
logic [31:0] pcpi_rs2;
logic project_wr;
logic [31:0] project_rd;
logic project_wait;
logic project_ready;
logic pcpi_wr;
logic [31:0] pcpi_rd;
logic pcpi_wait;
logic pcpi_ready;

pqc_pcpi_mlkem project_pcpi (
    .clk(clk),
    .resetn(resetn),
    .pcpi_valid(pcpi_valid && !STOCK_MUL),
    .pcpi_insn(pcpi_insn),
    .pcpi_rs1(pcpi_rs1),
    .pcpi_rs2(pcpi_rs2),
    .pcpi_wr(project_wr),
    .pcpi_rd(project_rd),
    .pcpi_wait(project_wait),
    .pcpi_ready(project_ready)
);

always_comb
begin
    if (STOCK_MUL)
    begin
        pcpi_wr = 1'b0;
        pcpi_rd = 32'b0;
        pcpi_wait = 1'b0;
        pcpi_ready = 1'b0;
    end
    else
    begin
        pcpi_wr = project_wr;
        pcpi_rd = project_rd;
        pcpi_wait = project_wait;
        pcpi_ready = project_ready;
    end
end

picorv32 #(
    .ENABLE_COUNTERS(1),
    .ENABLE_COUNTERS64(1),
    .ENABLE_REGS_16_31(1),
    .ENABLE_REGS_DUALPORT(1),
    .LATCHED_MEM_RDATA(0),
    .TWO_STAGE_SHIFT(1),
    .BARREL_SHIFTER(1),
    .TWO_CYCLE_COMPARE(0),
    .TWO_CYCLE_ALU(0),
    .COMPRESSED_ISA(1),
    .CATCH_MISALIGN(1),
    .CATCH_ILLINSN(1),
    .ENABLE_PCPI(1),
    .ENABLE_MUL(0),
    .ENABLE_FAST_MUL(STOCK_MUL),
    .ENABLE_DIV(1),
    .ENABLE_IRQ(0),
    .ENABLE_IRQ_QREGS(0),
    .ENABLE_IRQ_TIMER(0),
    .ENABLE_TRACE(0),
    .REGS_INIT_ZERO(0),
    .PROGADDR_RESET(32'h0000_0000),
    .PROGADDR_IRQ(32'h0000_0010),
    .STACKADDR(32'h0008_0000)
) core (
    .clk(clk),
    .resetn(resetn),
    .trap(trap),
    .mem_valid(mem_valid),
    .mem_instr(mem_instr),
    .mem_ready(mem_ready),
    .mem_addr(mem_addr),
    .mem_wdata(mem_wdata),
    .mem_wstrb(mem_wstrb),
    .mem_rdata(mem_rdata),
    .mem_la_read(),
    .mem_la_write(),
    .mem_la_addr(),
    .mem_la_wdata(),
    .mem_la_wstrb(),
    .pcpi_valid(pcpi_valid),
    .pcpi_insn(pcpi_insn),
    .pcpi_rs1(pcpi_rs1),
    .pcpi_rs2(pcpi_rs2),
    .pcpi_wr(pcpi_wr),
    .pcpi_rd(pcpi_rd),
    .pcpi_wait(pcpi_wait),
    .pcpi_ready(pcpi_ready),
    .irq(32'b0),
    .eoi(),
    .trace_valid(),
    .trace_data()
);

endmodule
