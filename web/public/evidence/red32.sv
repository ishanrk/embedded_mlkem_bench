module pqc_pcpi_mlkem #(
    parameter ENABLE_FQMUL = 1'b0,
    parameter ENABLE_RED32 = 1'b0,
    parameter ENABLE_FSRI = 1'b0
) (
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
    IDLE,
    PRODUCT,
    INVERSE,
    MODULUS,
    RESPONSE
} state_t;

state_t state;
logic served;
logic custom_request;
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

`ifdef FORMAL
assign formal_state = {state, served, custom_request, last_insn, last_rs1, last_rs2,
                       response_value, product_value, inverse_value, modulus_value};
assign formal_multiply_left = multiply_left;
assign formal_multiply_right = multiply_right;
assign formal_multiply_result = multiply_result;
assign formal_numerator = numerator;
assign formal_fqmul_result = fqmul_result;
`endif

always_comb
begin
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
    claim = m_claim || fqmul_claim || red32_claim || fsri_claim;
    same_request = pcpi_insn == last_insn && pcpi_rs1 == last_rs1 && pcpi_rs2 == last_rs2;

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

    multiply_left = 33'sd0;
    multiply_right = 33'sd0;
    if (state == PRODUCT)
    begin
        if (fsri_active)
        begin
            multiply_left = $signed({1'b0, last_rs1});
            multiply_right = $signed({1'b0, modulus_value});
        end
        else if (custom_request)
        begin
            multiply_left = $signed({{17{last_rs1[15]}}, last_rs1[15:0]});
            multiply_right = $signed({{17{last_rs2[15]}}, last_rs2[15:0]});
        end
        else
        begin
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
        if (fsri_active)
        begin
            multiply_left = $signed({1'b0, last_rs2});
            multiply_right = $signed({1'b0, modulus_value});
        end
        else
        begin
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

    numerator = $signed({product_value[31], product_value}) -
                $signed({modulus_value[31], modulus_value});
    fqmul_result = $signed({{15{numerator[32]}}, numerator[32:16]});

    pcpi_ready = state == RESPONSE && pcpi_valid && same_request;
    pcpi_wr = pcpi_ready;
    pcpi_rd = custom_request ? fqmul_result : response_value;
    if (state == IDLE)
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

always_ff @(posedge clk)
begin
    if (!resetn)
    begin
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
                    custom_request <= fqmul_claim || red32_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    if (fsri_claim)
                    begin
                        response_value <= pcpi_rs1;
                        modulus_value <= $signed(fsri_factor);
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
            end
            PRODUCT:
            begin
                if (fsri_active)
                begin
                    if (last_insn[29:25] != 5'b0)
                    begin
                        response_value <= multiply_result[63:32];
                    end
                    state <= INVERSE;
                end
                else if (custom_request)
                begin
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
                    if (last_insn[29:25] != 5'b0)
                    begin
                        response_value <= response_value | multiply_result[31:0];
                    end
                    state <= RESPONSE;
                end
                else
                begin
                    inverse_value <= multiply_result[15:0];
                    state <= MODULUS;
                end
            end
            MODULUS:
            begin
                modulus_value <= multiply_result[31:0];
                state <= RESPONSE;
            end
            RESPONSE:
            begin
                served <= 1'b1;
                if (claim && !same_request)
                begin
                    custom_request <= fqmul_claim || red32_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    if (fsri_claim)
                    begin
                        response_value <= pcpi_rs1;
                        modulus_value <= $signed(fsri_factor);
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
