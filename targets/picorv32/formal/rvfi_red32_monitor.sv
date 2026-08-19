module rvfi_red32_monitor (
    input logic        clock,
    input logic        reset,
    input logic        rvfi_valid,
    input logic [31:0] rvfi_insn,
    input logic        rvfi_trap,
    input logic [31:0] rvfi_rs1_rdata,
    input logic [31:0] rvfi_rs2_rdata,
    input logic [4:0]  rvfi_rd_addr,
    input logic [31:0] rvfi_rd_wdata,
    input logic [31:0] rvfi_pc_rdata,
    input logic [31:0] rvfi_pc_wdata,
    input logic [3:0]  rvfi_mem_rmask,
    input logic [3:0]  rvfi_mem_wmask,
    input logic        red32_accept,
    input logic [31:0] red32_left,
    input logic [31:0] red32_right,
    input logic [212:0] red32_state,
    input logic signed [31:0] red32_result,
    output logic       red32_retired = 1'b0
);

wire red32 = rvfi_valid && (rvfi_insn & 32'hfe00_707f) == 32'h0000_100b;
logic [2:0] reference_state = 3'b0;
logic [31:0] reference_left = 32'b0;
logic [31:0] reference_right = 32'b0;
logic signed [31:0] reference_product = 32'sd0;
logic [15:0] reference_inverse = 16'b0;
logic signed [31:0] reference_modulus = 32'sd0;
logic [31:0] reference_result = 32'b0;
logic reference_ready = 1'b0;
logic signed [32:0] inverse_left;
logic signed [65:0] inverse_product;
logic signed [32:0] modulus_left;
logic signed [65:0] modulus_product;
logic signed [32:0] numerator;

always_comb
begin
    inverse_left = $signed({17'b0, reference_product[15:0]});
    inverse_product = inverse_left * 33'sd62209;
    modulus_left = $signed({{17{reference_inverse[15]}}, reference_inverse});
    modulus_product = modulus_left * 33'sd3329;
    numerator = $signed({reference_product[31], reference_product}) -
                $signed({reference_modulus[31], reference_modulus});
end

always_ff @(posedge clock)
begin
    if (reset)
    begin
        red32_retired <= 1'b0;
        reference_state <= 3'b0;
        reference_ready <= 1'b0;
    end
    else
    begin
        if (red32_accept)
        begin
            reference_left <= red32_left;
            reference_right <= red32_right;
            reference_product <= $signed(red32_left);
            reference_state <= 3'd1;
            reference_ready <= 1'b0;
        end
        else if (reference_state == 3'd1)
        begin
            assume(red32_state[212:210] == 3'd2);
            assume(red32_state[79:48] == reference_product);
            reference_inverse <= inverse_product[15:0];
            reference_state <= 3'd2;
        end
        else if (reference_state == 3'd2)
        begin
            assume(red32_state[212:210] == 3'd3);
            assume(red32_state[47:32] == reference_inverse);
            reference_modulus <= modulus_product[31:0];
            reference_state <= 3'd3;
        end
        else if (reference_state == 3'd3)
        begin
            assume(red32_state[212:210] == 3'd4);
            assume(red32_state[31:0] == reference_modulus);
            reference_result <= {{15{numerator[32]}}, numerator[32:16]};
            reference_state <= 3'b0;
            reference_ready <= 1'b1;
        end

        if (red32)
        begin
            assume(red32_result == reference_result);
            red32_retired <= 1'b1;
            assert(reference_ready);
            assert(rvfi_rs1_rdata == reference_left);
            assert(rvfi_rs2_rdata == reference_right);
            assert(!rvfi_trap);
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
                assert(rvfi_rd_wdata == reference_result);
            end
        end
    end
end

endmodule

module rvfi_red32_formal (
    input logic clk
);

logic resetn = 1'b0;
logic [5:0] cycle = 6'b0;
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
logic formal_red32_accept;
logic [31:0] formal_red32_left;
logic [31:0] formal_red32_right;
logic [212:0] formal_red32_state;
logic signed [31:0] formal_red32_result;
logic red32_retired;

assign mem_ready = mem_valid;
assign mem_rdata = mem_addr == 32'b0 ? custom_insn : 32'h0000_006f;

pqc_picorv32_core_top #(
    .ENABLE_FQMUL(1'b1),
    .ENABLE_RED32(1'b1)
) core (
    .clk(clk), .resetn(resetn), .trap(trap), .mem_valid(mem_valid),
    .mem_instr(mem_instr), .mem_ready(mem_ready), .mem_addr(mem_addr),
    .mem_wdata(mem_wdata), .mem_wstrb(mem_wstrb), .mem_rdata(mem_rdata),
    .rvfi_valid(rvfi_valid), .rvfi_order(rvfi_order), .rvfi_insn(rvfi_insn),
    .rvfi_trap(rvfi_trap), .rvfi_halt(rvfi_halt), .rvfi_intr(rvfi_intr),
    .rvfi_rs1_addr(rvfi_rs1_addr), .rvfi_rs2_addr(rvfi_rs2_addr),
    .rvfi_rs1_rdata(rvfi_rs1_rdata), .rvfi_rs2_rdata(rvfi_rs2_rdata),
    .rvfi_rd_addr(rvfi_rd_addr), .rvfi_rd_wdata(rvfi_rd_wdata),
    .rvfi_pc_rdata(rvfi_pc_rdata), .rvfi_pc_wdata(rvfi_pc_wdata),
    .rvfi_mem_addr(rvfi_mem_addr), .rvfi_mem_rmask(rvfi_mem_rmask),
    .rvfi_mem_wmask(rvfi_mem_wmask), .rvfi_mem_rdata(rvfi_mem_rdata),
    .rvfi_mem_wdata(rvfi_mem_wdata), .formal_red32_accept(formal_red32_accept),
    .formal_red32_left(formal_red32_left), .formal_red32_right(formal_red32_right),
    .formal_red32_state(formal_red32_state), .formal_red32_result(formal_red32_result)
);

rvfi_red32_monitor monitor (
    .clock(clk), .reset(!resetn), .rvfi_valid(rvfi_valid), .rvfi_insn(rvfi_insn),
    .rvfi_trap(rvfi_trap), .rvfi_rs1_rdata(rvfi_rs1_rdata),
    .rvfi_rs2_rdata(rvfi_rs2_rdata), .rvfi_rd_addr(rvfi_rd_addr),
    .rvfi_rd_wdata(rvfi_rd_wdata), .rvfi_pc_rdata(rvfi_pc_rdata),
    .rvfi_pc_wdata(rvfi_pc_wdata), .rvfi_mem_rmask(rvfi_mem_rmask),
    .rvfi_mem_wmask(rvfi_mem_wmask), .red32_accept(formal_red32_accept),
    .red32_left(formal_red32_left), .red32_right(formal_red32_right),
    .red32_state(formal_red32_state), .red32_result(formal_red32_result),
    .red32_retired(red32_retired)
);

always_ff @(posedge clk)
begin
    assume((custom_insn & 32'hfe00_707f) == 32'h0000_100b);
    resetn <= 1'b1;
    if (resetn && cycle != 6'h3f)
    begin
        cycle <= cycle + 1'b1;
    end
    if (cycle == 6'd20)
    begin
        assert(red32_retired);
    end
    if (resetn)
    begin
        assert(rvfi_mem_wmask == 4'b0 || !red32_retired);
        cover(red32_retired);
    end
end

endmodule
