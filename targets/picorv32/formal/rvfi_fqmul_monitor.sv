module rvfi_fqmul_monitor (
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
    input logic        fqmul_accept,
    input logic [31:0] fqmul_left,
    input logic [31:0] fqmul_right,
    input logic [212:0] fqmul_state,
    input logic signed [31:0] fqmul_result,
    output logic       fqmul_retired = 1'b0
);

wire fqmul = rvfi_valid && (rvfi_insn & 32'hfe00_707f) == 32'h0000_000b;
logic [2:0] reference_state = 3'b0;
logic [31:0] reference_left = 32'b0;
logic [31:0] reference_right = 32'b0;
logic signed [31:0] reference_product = 32'sd0;
logic [15:0] reference_inverse = 16'b0;
logic signed [31:0] reference_modulus = 32'sd0;
logic [31:0] reference_result = 32'b0;
logic reference_ready = 1'b0;
logic signed [32:0] accept_left;
logic signed [32:0] accept_right;
logic signed [65:0] accept_product;
logic signed [32:0] inverse_left;
logic signed [65:0] inverse_product;
logic signed [32:0] modulus_left;
logic signed [65:0] modulus_product;
logic signed [32:0] numerator;

always_comb
begin
    accept_left = $signed({{17{fqmul_left[15]}}, fqmul_left[15:0]});
    accept_right = $signed({{17{fqmul_right[15]}}, fqmul_right[15:0]});
    accept_product = accept_left * accept_right;
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
        fqmul_retired <= 1'b0;
        reference_state <= 3'b0;
        reference_ready <= 1'b0;
    end
    else
    begin
        if (fqmul_accept)
        begin
            reference_left <= fqmul_left;
            reference_right <= fqmul_right;
            reference_product <= accept_product[31:0];
            reference_state <= 3'd1;
            reference_ready <= 1'b0;
        end
        else if (reference_state == 3'd1)
        begin
            assume(fqmul_state[212:210] == 3'd1);
            reference_inverse <= inverse_product[15:0];
            reference_state <= 3'd2;
        end
        else if (reference_state == 3'd2)
        begin
            assume(fqmul_state[212:210] == 3'd2);
            assume(fqmul_state[79:48] == reference_product);
            reference_modulus <= modulus_product[31:0];
            reference_state <= 3'd3;
        end
        else if (reference_state == 3'd3)
        begin
            assume(fqmul_state[212:210] == 3'd3);
            assume(fqmul_state[47:32] == reference_inverse);
            reference_result <= {{15{numerator[32]}}, numerator[32:16]};
            reference_state <= 3'b0;
            reference_ready <= 1'b1;
        end

        if (fqmul)
        begin
            assume(fqmul_state[212:210] == 3'd0);
            assume(fqmul_result == reference_result);
            fqmul_retired <= 1'b1;
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
                for (integer bit_index = 0; bit_index < 32; bit_index = bit_index + 1)
                begin
                    assert(rvfi_rd_wdata[bit_index] == reference_result[bit_index]);
                end
            end
        end
    end
end

endmodule

module rvfi_fqmul_formal (
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
logic formal_fqmul_accept;
logic [31:0] formal_fqmul_left;
logic [31:0] formal_fqmul_right;
logic [212:0] formal_fqmul_state;
logic signed [31:0] formal_fqmul_result;
logic fqmul_retired;

assign mem_ready = mem_valid;
assign mem_rdata = mem_addr == 32'b0 ? custom_insn : 32'h0000_006f;

pqc_picorv32_core_top #(
    .ENABLE_FQMUL(1'b1)
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
    .rvfi_mem_wdata(rvfi_mem_wdata), .formal_fqmul_accept(formal_fqmul_accept),
    .formal_fqmul_left(formal_fqmul_left), .formal_fqmul_right(formal_fqmul_right),
    .formal_fqmul_state(formal_fqmul_state), .formal_fqmul_result(formal_fqmul_result)
);

rvfi_fqmul_monitor monitor (
    .clock(clk), .reset(!resetn), .rvfi_valid(rvfi_valid), .rvfi_insn(rvfi_insn),
    .rvfi_trap(rvfi_trap), .rvfi_rs1_rdata(rvfi_rs1_rdata),
    .rvfi_rs2_rdata(rvfi_rs2_rdata), .rvfi_rd_addr(rvfi_rd_addr),
    .rvfi_rd_wdata(rvfi_rd_wdata), .rvfi_pc_rdata(rvfi_pc_rdata),
    .rvfi_pc_wdata(rvfi_pc_wdata), .rvfi_mem_rmask(rvfi_mem_rmask),
    .rvfi_mem_wmask(rvfi_mem_wmask), .fqmul_accept(formal_fqmul_accept),
    .fqmul_left(formal_fqmul_left), .fqmul_right(formal_fqmul_right),
    .fqmul_state(formal_fqmul_state), .fqmul_result(formal_fqmul_result),
    .fqmul_retired(fqmul_retired)
);

always_ff @(posedge clk)
begin
    assume((custom_insn & 32'hfe00_707f) == 32'h0000_000b);
    resetn <= 1'b1;
    if (resetn && cycle != 6'h3f)
    begin
        cycle <= cycle + 1'b1;
    end
    if (cycle == 6'd20)
    begin
        assert(fqmul_retired);
    end
    if (resetn)
    begin
        assert(rvfi_mem_wmask == 4'b0 || !fqmul_retired);
        cover(fqmul_retired);
    end
end

endmodule
