module pqc_picorv32_core_top #(
    parameter STOCK_MUL = 1'b0,
    parameter ENABLE_FQMUL = 1'b0,
    parameter ENABLE_RED32 = 1'b0,
    parameter ENABLE_FSRI = 1'b0
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
`ifdef RISCV_FORMAL
    ,
    output logic        rvfi_valid,
    output logic [63:0] rvfi_order,
    output logic [31:0] rvfi_insn,
    output logic        rvfi_trap,
    output logic        rvfi_halt,
    output logic        rvfi_intr,
    output logic [4:0]  rvfi_rs1_addr,
    output logic [4:0]  rvfi_rs2_addr,
    output logic [31:0] rvfi_rs1_rdata,
    output logic [31:0] rvfi_rs2_rdata,
    output logic [4:0]  rvfi_rd_addr,
    output logic [31:0] rvfi_rd_wdata,
    output logic [31:0] rvfi_pc_rdata,
    output logic [31:0] rvfi_pc_wdata,
    output logic [31:0] rvfi_mem_addr,
    output logic [3:0]  rvfi_mem_rmask,
    output logic [3:0]  rvfi_mem_wmask,
    output logic [31:0] rvfi_mem_rdata,
    output logic [31:0] rvfi_mem_wdata,
    output logic        formal_fqmul_accept,
    output logic [31:0] formal_fqmul_left,
    output logic [31:0] formal_fqmul_right,
    output logic [212:0] formal_fqmul_state,
    output logic signed [31:0] formal_fqmul_result,
    output logic        formal_red32_accept,
    output logic [31:0] formal_red32_left,
    output logic [31:0] formal_red32_right,
    output logic [212:0] formal_red32_state,
    output logic signed [31:0] formal_red32_result
`endif
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
`ifdef RISCV_FORMAL
logic [212:0] project_formal_state;
logic signed [31:0] project_formal_result;
logic [31:0] core_rvfi_rs1_rdata;
logic [31:0] core_rvfi_rs2_rdata;
`endif

pqc_pcpi_mlkem #(
    .ENABLE_FQMUL(ENABLE_FQMUL),
    .ENABLE_RED32(ENABLE_RED32),
    .ENABLE_FSRI(ENABLE_FSRI)
) project_pcpi (
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
`ifdef RISCV_FORMAL
    , .formal_state(project_formal_state),
    .formal_fqmul_result(project_formal_result)
`endif
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
`ifdef RISCV_FORMAL
    .rvfi_valid(rvfi_valid),
    .rvfi_order(rvfi_order),
    .rvfi_insn(rvfi_insn),
    .rvfi_trap(rvfi_trap),
    .rvfi_halt(rvfi_halt),
    .rvfi_intr(rvfi_intr),
    .rvfi_mode(),
    .rvfi_ixl(),
    .rvfi_rs1_addr(rvfi_rs1_addr),
    .rvfi_rs2_addr(rvfi_rs2_addr),
    .rvfi_rs1_rdata(core_rvfi_rs1_rdata),
    .rvfi_rs2_rdata(core_rvfi_rs2_rdata),
    .rvfi_rd_addr(rvfi_rd_addr),
    .rvfi_rd_wdata(rvfi_rd_wdata),
    .rvfi_pc_rdata(rvfi_pc_rdata),
    .rvfi_pc_wdata(rvfi_pc_wdata),
    .rvfi_mem_addr(rvfi_mem_addr),
    .rvfi_mem_rmask(rvfi_mem_rmask),
    .rvfi_mem_wmask(rvfi_mem_wmask),
    .rvfi_mem_rdata(rvfi_mem_rdata),
    .rvfi_mem_wdata(rvfi_mem_wdata),
    .rvfi_csr_mcycle_rmask(),
    .rvfi_csr_mcycle_wmask(),
    .rvfi_csr_mcycle_rdata(),
    .rvfi_csr_mcycle_wdata(),
    .rvfi_csr_minstret_rmask(),
    .rvfi_csr_minstret_wmask(),
    .rvfi_csr_minstret_rdata(),
    .rvfi_csr_minstret_wdata(),
`endif
    .irq(32'b0),
    .eoi(),
    .trace_valid(),
    .trace_data()
);

`ifdef RISCV_FORMAL
always_comb
begin
    rvfi_rs1_rdata = core_rvfi_rs1_rdata;
    rvfi_rs2_rdata = core_rvfi_rs2_rdata;
    if (rvfi_valid && ((rvfi_insn & 32'hfe00_707f) == 32'h0000_000b ||
                       (rvfi_insn & 32'hfe00_707f) == 32'h0000_100b ||
                       (rvfi_insn & 32'hc000_707f) == 32'h0000_200b))
    begin
        rvfi_rs1_rdata = project_formal_state[175:144];
        rvfi_rs2_rdata = project_formal_state[143:112];
    end
end
assign formal_fqmul_accept = pcpi_valid &&
                             (pcpi_insn & 32'hfe00_707f) == 32'h0000_000b &&
                             project_wait && project_formal_state[212:210] == 3'd0;
assign formal_fqmul_left = pcpi_rs1;
assign formal_fqmul_right = pcpi_rs2;
assign formal_fqmul_state = project_formal_state;
assign formal_fqmul_result = project_formal_result;
assign formal_red32_accept = pcpi_valid &&
                             (pcpi_insn & 32'hfe00_707f) == 32'h0000_100b &&
                             project_wait && project_formal_state[212:210] == 3'd0;
assign formal_red32_left = pcpi_rs1;
assign formal_red32_right = pcpi_rs2;
assign formal_red32_state = project_formal_state;
assign formal_red32_result = project_formal_result;
`endif

endmodule
