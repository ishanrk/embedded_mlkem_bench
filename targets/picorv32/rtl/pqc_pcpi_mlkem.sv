module pqc_pcpi_mlkem #(
    parameter ENABLE_FQMUL = 1'b0
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
    claim = m_claim || fqmul_claim;
    same_request = pcpi_insn == last_insn && pcpi_rs1 == last_rs1 && pcpi_rs2 == last_rs2;

    multiply_left = 33'sd0;
    multiply_right = 33'sd0;
    if (state == PRODUCT)
    begin
        if (custom_request)
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
        multiply_left = $signed({17'b0, product_value[15:0]});
        multiply_right = 33'sd62209;
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
                    custom_request <= fqmul_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    state <= PRODUCT;
                end
            end
            PRODUCT:
            begin
                if (custom_request)
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
                inverse_value <= multiply_result[15:0];
                state <= MODULUS;
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
                    custom_request <= fqmul_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    state <= PRODUCT;
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
