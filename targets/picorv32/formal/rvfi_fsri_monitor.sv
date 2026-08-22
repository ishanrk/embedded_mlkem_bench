module rvfi_fsri_formal (
    input logic clk
);

logic resetn = 1'b0;
logic [5:0] cycle = 6'b0;
logic retired = 1'b0;
(* anyconst *) logic [31:0] custom_insn;
logic trap;
logic mem_valid;
logic mem_instr;
logic mem_ready;
logic [31:0] mem_addr;
logic [31:0] mem_wdata;
logic [3:0] mem_wstrb;
logic [31:0] mem_rdata;
logic rvfi_valid;
logic [63:0] rvfi_order;
logic [31:0] rvfi_insn;
logic rvfi_trap;
logic rvfi_halt;
logic rvfi_intr;
logic [4:0] rvfi_rs1_addr;
logic [4:0] rvfi_rs2_addr;
logic [31:0] rvfi_rs1_rdata;
logic [31:0] rvfi_rs2_rdata;
logic [4:0] rvfi_rd_addr;
logic [31:0] rvfi_rd_wdata;
logic [31:0] rvfi_pc_rdata;
logic [31:0] rvfi_pc_wdata;
logic [31:0] rvfi_mem_addr;
logic [3:0] rvfi_mem_rmask;
logic [3:0] rvfi_mem_wmask;
logic [31:0] rvfi_mem_rdata;
logic [31:0] rvfi_mem_wdata;
logic [63:0] expected;

wire fsri = cycle >= 6'd2 && rvfi_valid &&
            (rvfi_insn & 32'hc000_707f) == 32'h0000_200b;

assign mem_ready = mem_valid;
assign mem_rdata = mem_addr == 32'b0 ? custom_insn : 32'h0000_006f;
assign expected = {rvfi_rs2_rdata, rvfi_rs1_rdata} >> rvfi_insn[29:25];

pqc_picorv32_core_top #(
    .ENABLE_FSRI(1'b1)
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
    .rvfi_valid(rvfi_valid),
    .rvfi_order(rvfi_order),
    .rvfi_insn(rvfi_insn),
    .rvfi_trap(rvfi_trap),
    .rvfi_halt(rvfi_halt),
    .rvfi_intr(rvfi_intr),
    .rvfi_rs1_addr(rvfi_rs1_addr),
    .rvfi_rs2_addr(rvfi_rs2_addr),
    .rvfi_rs1_rdata(rvfi_rs1_rdata),
    .rvfi_rs2_rdata(rvfi_rs2_rdata),
    .rvfi_rd_addr(rvfi_rd_addr),
    .rvfi_rd_wdata(rvfi_rd_wdata),
    .rvfi_pc_rdata(rvfi_pc_rdata),
    .rvfi_pc_wdata(rvfi_pc_wdata),
    .rvfi_mem_addr(rvfi_mem_addr),
    .rvfi_mem_rmask(rvfi_mem_rmask),
    .rvfi_mem_wmask(rvfi_mem_wmask),
    .rvfi_mem_rdata(rvfi_mem_rdata),
    .rvfi_mem_wdata(rvfi_mem_wdata)
);

always_ff @(posedge clk)
begin
    assume((custom_insn & 32'hc000_707f) == 32'h0000_200b);
    resetn <= 1'b1;
    if (resetn && cycle != 6'h3f)
    begin
        cycle <= cycle + 1'b1;
    end

    if (fsri)
    begin
        retired <= 1'b1;
        assert(!rvfi_trap);
        assert(rvfi_rs1_addr == rvfi_insn[19:15]);
        assert(rvfi_rs2_addr == rvfi_insn[24:20]);
        assert(rvfi_pc_wdata == rvfi_pc_rdata + 32'd4);
        assert(rvfi_mem_rmask == 4'b0);
        assert(rvfi_mem_wmask == 4'b0);
        assert(rvfi_rd_addr == rvfi_insn[11:7]);
        if (rvfi_insn[11:7] == 5'b0)
        begin
            assert(rvfi_rd_wdata == 32'b0);
        end
        else
        begin
            assert(rvfi_rd_wdata == expected[31:0]);
        end
    end

    if (cycle == 6'd20)
    begin
        assert(retired);
    end
end

endmodule
