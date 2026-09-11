// PCPI coprocessor shared by normal RV32M multiply and the three experiment instructions
// PicoRV32 holds valid high until this block returns ready or declines the instruction
module pqc_pcpi_mlkem #(
    // module parameters let synthesis remove instruction paths not used by an experiment
    parameter ENABLE_FQMUL = 1'b0,
    parameter ENABLE_RED32 = 1'b0,
    parameter ENABLE_FSRI = 1'b0,
    parameter FSRI_IMPL = 0
) (
    input  logic        clk,
    input  logic        resetn,
    // valid presents insn and its two register values; wait says we claimed but need more cycles
    input  logic        pcpi_valid,
    input  logic [31:0] pcpi_insn,
    input  logic [31:0] pcpi_rs1,
    input  logic [31:0] pcpi_rs2,
    // ready completes the request; wr says rd contains a value for the destination register
    output logic        pcpi_wr,
    output logic [31:0] pcpi_rd,
    output logic        pcpi_wait,
    output logic        pcpi_ready
`ifdef FORMAL
    , output logic [212:0] formal_state,
    output logic signed [32:0] formal_multiply_left,
    output logic signed [32:0] formal_multiply_right,
    output logic signed [65:0] formal_multiply_result,
    output logic signed [32:0] formal_numerator,
    output logic signed [31:0] formal_fqmul_result
`endif
);

typedef enum logic [2:0]
{
    // decode and accept a new request
    IDLE,
    // FQMUL/normal MUL product, or first FSRI reuse step
    PRODUCT,
    // low16(product)*62209, or second FSRI step
    INVERSE,
    // signed low16(inverse)*3329
    MODULUS,
    // hold the finished result for PicoRV32
    RESPONSE
} state_t;

// state and the latched request below are registers updated only in always_ff
state_t state;
// remembers a response until PicoRV32 drops or changes the still-valid request
logic served;
logic custom_request;
// decode/claim signals below are combinational wires recalculated from current inputs
logic m_claim;
logic fqmul_claim;
logic red32_claim;
logic fsri_claim;
logic fsri_active;
logic claim;
logic same_request;
logic [31:0] last_insn;
logic [31:0] last_rs1;
logic [31:0] last_rs2;
// one shared multiply datapath handles RV32M, FQMUL, RED32, and FSRI reuse
logic signed [32:0] multiply_left;
logic signed [32:0] multiply_right;
logic signed [65:0] multiply_result;
logic [31:0] response_value;
logic signed [31:0] product_value;
logic [15:0] inverse_value;
logic signed [31:0] modulus_value;
logic signed [32:0] numerator;
logic signed [31:0] fqmul_result;
logic [31:0] m_result;
logic [15:0] fsri_half;
logic [31:0] fsri_factor;
logic [31:0] fsri_window;
logic [31:0] fsri_shifted;
logic [63:0] fsri_direct;
logic fsri_direct_claim;

`ifdef FORMAL
assign formal_state = {state, served, custom_request, last_insn, last_rs1, last_rs2,
                       response_value, product_value, inverse_value, modulus_value};
assign formal_multiply_left = multiply_left;
assign formal_multiply_right = multiply_right;
assign formal_multiply_result = multiply_result;
assign formal_numerator = numerator;
assign formal_fqmul_result = fqmul_result;
`endif

// always_comb describes wires: no values here survive a clock edge on their own
always_comb
begin
    // masks keep opcode/funct bits and ignore rd/rs fields; funct3 separates custom operations
    m_claim = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
              pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b0;
    fqmul_claim = ENABLE_FQMUL && pcpi_valid &&
                  (pcpi_insn & 32'hfe00_707f) == 32'h0000_000b;
    red32_claim = ENABLE_RED32 && pcpi_valid &&
                  (pcpi_insn & 32'hfe00_707f) == 32'h0000_100b;
    fsri_claim = ENABLE_FSRI && pcpi_valid &&
                 (pcpi_insn & 32'hc000_707f) == 32'h0000_200b;
    fsri_active = ENABLE_FSRI &&
                  (last_insn & 32'hc000_707f) == 32'h0000_200b;
    // direct FSRI finishes here without entering the clocked FSM
    fsri_direct_claim = fsri_claim && FSRI_IMPL == 2;
    claim = m_claim || fqmul_claim || red32_claim || (fsri_claim && FSRI_IMPL != 2);
    same_request = pcpi_insn == last_insn && pcpi_rs1 == last_rs1 && pcpi_rs2 == last_rs2;

    // multiplier-reuse FSRI builds 2^-shift as a one-hot factor in two 16-bit pieces
    case (pcpi_insn[28:25])
        4'd0: fsri_half = 16'h0001;
        4'd1: fsri_half = 16'h8000;
        4'd2: fsri_half = 16'h4000;
        4'd3: fsri_half = 16'h2000;
        4'd4: fsri_half = 16'h1000;
        4'd5: fsri_half = 16'h0800;
        4'd6: fsri_half = 16'h0400;
        4'd7: fsri_half = 16'h0200;
        4'd8: fsri_half = 16'h0100;
        4'd9: fsri_half = 16'h0080;
        4'd10: fsri_half = 16'h0040;
        4'd11: fsri_half = 16'h0020;
        4'd12: fsri_half = 16'h0010;
        4'd13: fsri_half = 16'h0008;
        4'd14: fsri_half = 16'h0004;
        default: fsri_half = 16'h0002;
    endcase
    fsri_factor = 32'b0;
    if (pcpi_insn[29:25] != 5'b0)
    begin
        if (!pcpi_insn[29] || pcpi_insn[28:25] == 4'b0)
        begin
            fsri_factor = {fsri_half, 16'b0};
        end
        else
        begin
            fsri_factor = {16'b0, fsri_half};
        end
    end

    // sliced FSRI (impl 1) extracts the low and high result halves on consecutive states
    fsri_window = last_insn[29] ? {last_rs2[15:0], last_rs1[31:16]} : last_rs1;
    if (state == INVERSE)
    begin
        fsri_window = last_insn[29] ? last_rs2 : {last_rs2[15:0], last_rs1[31:16]};
    end
    fsri_shifted = fsri_window >> last_insn[28:25];
    // direct implementation is just a 64-bit combinational funnel and has zero PCPI wait cycles
    fsri_direct = {pcpi_rs2, pcpi_rs1} >> pcpi_insn[29:25];

    // choose operands for the multiplier according to the current FSM step
    multiply_left = 33'sd0;
    multiply_right = 33'sd0;
    if (state == PRODUCT)
    begin
        if (fsri_active && FSRI_IMPL == 0)
        begin
            multiply_left = $signed({1'b0, last_rs1});
            multiply_right = $signed({1'b0, modulus_value});
        end
        else if (custom_request)
        begin
            // FQMUL uses signed low halves; upper register bits are intentionally ignored
            multiply_left = $signed({{17{last_rs1[15]}}, last_rs1[15:0]});
            multiply_right = $signed({{17{last_rs2[15]}}, last_rs2[15:0]});
        end
        else
        begin
            // sign extension follows MUL/MULH/MULHSU/MULHU funct3 rules
            multiply_left = $signed({1'b0, last_rs1});
            multiply_right = $signed({1'b0, last_rs2});
            if (last_insn[13:12] == 2'b01 || last_insn[13:12] == 2'b10)
            begin
                multiply_left = $signed({last_rs1[31], last_rs1});
            end
            if (last_insn[13:12] == 2'b01)
            begin
                multiply_right = $signed({last_rs2[31], last_rs2});
            end
        end
    end
    else if (state == INVERSE)
    begin
        if (fsri_active && FSRI_IMPL == 0)
        begin
            multiply_left = $signed({1'b0, last_rs2});
            multiply_right = $signed({1'b0, modulus_value});
        end
        else
        begin
            // RED32 arrives directly here because rs1 already contains the normal MUL result
            multiply_left = $signed({17'b0, product_value[15:0]});
            multiply_right = 33'sd62209;
        end
    end
    else if (state == MODULUS)
    begin
        multiply_left = $signed({{17{inverse_value[15]}}, inverse_value});
        multiply_right = 33'sd3329;
    end
    multiply_result = multiply_left * multiply_right;

    if (last_insn[14:12] == 3'b000)
    begin
        m_result = multiply_result[31:0];
    end
    else
    begin
        m_result = multiply_result[63:32];
    end

    // Montgomery result is exactly (product - signed_inverse*q) / 2^16
    numerator = $signed({product_value[31], product_value}) -
                $signed({modulus_value[31], modulus_value});
    fqmul_result = $signed({{15{numerator[32]}}, numerator[32:16]});

    // only answer the request whose operands were latched; avoids stale back-to-back responses
    pcpi_ready = fsri_direct_claim || (state == RESPONSE && pcpi_valid && same_request);
    pcpi_wr = pcpi_ready;
    pcpi_rd = fsri_direct_claim ? fsri_direct[31:0] :
              custom_request ? fqmul_result : response_value;
    if (fsri_direct_claim)
    begin
        pcpi_wait = 1'b0;
    end
    else if (state == IDLE)
    begin
        pcpi_wait = claim && (!served || !same_request);
    end
    else if (state == RESPONSE)
    begin
        pcpi_wait = claim && !same_request;
    end
    else
    begin
        pcpi_wait = pcpi_valid && same_request;
    end
end

// always_ff is register logic; nonblocking <= makes all registers update together at the edge
always_ff @(posedge clk)
begin
    if (!resetn)
    begin
        // synchronous active-low reset cancels work and clears any saved response
        state <= IDLE;
        served <= 1'b0;
        custom_request <= 1'b0;
        last_insn <= 32'b0;
        last_rs1 <= 32'b0;
        last_rs2 <= 32'b0;
        response_value <= 32'b0;
        product_value <= 32'sd0;
        inverse_value <= 16'b0;
        modulus_value <= 32'sd0;
    end
    else
    begin
        case (state)
            IDLE:
            begin
                if (!pcpi_valid)
                begin
                    served <= 1'b0;
                end
                if (claim && (!served || !same_request))
                begin
                    // PCPI inputs may change later, so every multicycle request is latched here
                    custom_request <= fqmul_claim || red32_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    if (fsri_claim)
                    begin
                        // impl 0 reuses PicoRV32's multiplier; impl 1 uses two sliced shifts
                        response_value <= pcpi_rs1;
                        modulus_value <= FSRI_IMPL == 0 ? $signed(fsri_factor) : 32'sd0;
                        state <= PRODUCT;
                    end
                    else if (red32_claim)
                    begin
                        // skip PRODUCT: firmware already issued the ordinary RISC-V MUL
                        product_value <= $signed(pcpi_rs1);
                        state <= INVERSE;
                    end
                    else
                    begin
                        state <= PRODUCT;
                    end
                end
            end
            PRODUCT:
            begin
                if (fsri_active)
                begin
                    if (FSRI_IMPL == 1)
                    begin
                        response_value[15:0] <= fsri_shifted[15:0];
                    end
                    else if (last_insn[29:25] != 5'b0)
                    begin
                        response_value <= multiply_result[63:32];
                    end
                    state <= INVERSE;
                end
                else if (custom_request)
                begin
                    // first of FQMUL's three multiplications
                    product_value <= multiply_result[31:0];
                    state <= INVERSE;
                end
                else
                begin
                    response_value <= m_result;
                    state <= RESPONSE;
                end
            end
            INVERSE:
            begin
                if (fsri_active)
                begin
                    if (FSRI_IMPL == 1)
                    begin
                        response_value[31:16] <= fsri_shifted[15:0];
                    end
                    else if (last_insn[29:25] != 5'b0)
                    begin
                        response_value <= response_value | multiply_result[31:0];
                    end
                    state <= RESPONSE;
                end
                else
                begin
                    // low half is enough because Montgomery arithmetic is modulo 2^16 here
                    inverse_value <= multiply_result[15:0];
                    state <= MODULUS;
                end
            end
            MODULUS:
            begin
                // store inverse*q; RESPONSE forms and returns the shifted numerator
                modulus_value <= multiply_result[31:0];
                state <= RESPONSE;
            end
            RESPONSE:
            begin
                served <= 1'b1;
                if (claim && !same_request)
                begin
                    // accept a new request immediately after answering the previous one
                    custom_request <= fqmul_claim || red32_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    if (fsri_claim)
                    begin
                        response_value <= pcpi_rs1;
                        modulus_value <= FSRI_IMPL == 0 ? $signed(fsri_factor) : 32'sd0;
                        state <= PRODUCT;
                    end
                    else if (red32_claim)
                    begin
                        product_value <= $signed(pcpi_rs1);
                        state <= INVERSE;
                    end
                    else
                    begin
                        state <= PRODUCT;
                    end
                end
                else
                begin
                    state <= IDLE;
                end
            end
            default:
            begin
                state <= IDLE;
                served <= 1'b0;
            end
        endcase
    end
end

endmodule
