// pcpi coprocessor implementing normal rv32 multiplication and the optional mlkem instructions
module pqc_pcpi_mlkem #(
    parameter ENABLE_FQMUL = 1'b0,
    parameter ENABLE_RED32 = 1'b0,
    parameter ENABLE_FSRI = 1'b0,
    parameter ENABLE_DOT2X = 1'b0
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
);

localparam [2:0] IDLE = 3'd0;
localparam [2:0] PRODUCT = 3'd1;
localparam [2:0] INVERSE = 3'd2;
localparam [2:0] MODULUS = 3'd3;
localparam [2:0] RESPONSE = 3'd4;
localparam [2:0] DOT_PRODUCT = 3'd5;

// saved values keep a multicycle request stable after PicoRV32 moves on
logic [2:0] state;
logic served;
logic custom_request;
logic dot2x_request;
logic [31:0] last_insn;
logic [31:0] last_rs1;
logic [31:0] last_rs2;
logic [31:0] response_value;
logic signed [31:0] product_value;
logic [15:0] inverse_value;
logic signed [31:0] modulus_value;

// these values only depend on current inputs and registers
logic multiply_claim;
logic fqmul_claim;
logic red32_claim;
logic fsri_claim;
logic dot2x_claim;
logic sequential_claim;
logic same_request;
logic signed [32:0] multiply_left;
logic signed [32:0] multiply_right;
logic signed [65:0] multiply_result;
logic [31:0] multiply_response;
logic signed [32:0] numerator;
logic signed [31:0] reduced_result;
logic [63:0] fsri_result;

always_comb
begin
    // masks ignore register fields while checking the operation fields
    multiply_claim = pcpi_valid && pcpi_insn[6:0] == 7'b0110011 &&
                     pcpi_insn[31:25] == 7'b0000001 && pcpi_insn[14] == 1'b0;
    fqmul_claim = ENABLE_FQMUL && pcpi_valid &&
                  (pcpi_insn & 32'hfe00_707f) == 32'h0000_000b;
    red32_claim = ENABLE_RED32 && pcpi_valid &&
                  (pcpi_insn & 32'hfe00_707f) == 32'h0000_100b;
    fsri_claim = ENABLE_FSRI && pcpi_valid &&
                 (pcpi_insn & 32'hc000_707f) == 32'h0000_200b;
    dot2x_claim = ENABLE_DOT2X && pcpi_valid &&
                  (pcpi_insn & 32'hfe00_707f) == 32'h0000_300b;
    sequential_claim = multiply_claim || fqmul_claim || red32_claim;
    if (ENABLE_DOT2X)
    begin
        sequential_claim = sequential_claim || dot2x_claim;
    end
    same_request = pcpi_insn == last_insn && pcpi_rs1 == last_rs1 &&
                   pcpi_rs2 == last_rs2;

    multiply_left = 33'sd0;
    multiply_right = 33'sd0;
    if (state == PRODUCT)
    begin
        if (dot2x_request)
        begin
            multiply_left = $signed({{17{last_rs1[15]}}, last_rs1[15:0]});
            multiply_right = $signed({{17{last_rs2[31]}}, last_rs2[31:16]});
        end
        else if (custom_request)
        begin
            // FQMUL uses the signed low half of each source register
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
    else if (ENABLE_DOT2X && state == DOT_PRODUCT)
    begin
        multiply_left = $signed({{17{last_rs1[31]}}, last_rs1[31:16]});
        multiply_right = $signed({{17{last_rs2[15]}}, last_rs2[15:0]});
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

    multiply_response = last_insn[14:12] == 3'b000
                            ? multiply_result[31:0]
                            : multiply_result[63:32];

    // upper half of this exact difference is the Montgomery result
    numerator = $signed({product_value[31], product_value}) -
                $signed({modulus_value[31], modulus_value});
    reduced_result = $signed({{15{numerator[32]}}, numerator[32:16]});

    // direct FSRI needs no saved state or multiplier cycle
    fsri_result = {pcpi_rs2, pcpi_rs1} >> pcpi_insn[29:25];

    pcpi_ready = fsri_claim ||
                 (state == RESPONSE && pcpi_valid && same_request);
    pcpi_wr = pcpi_ready;
    pcpi_rd = fsri_claim ? fsri_result[31:0] :
              custom_request ? reduced_result : response_value;

    if (fsri_claim)
    begin
        pcpi_wait = 1'b0;
    end
    else if (state == IDLE)
    begin
        pcpi_wait = sequential_claim && (!served || !same_request);
    end
    else if (state == RESPONSE)
    begin
        pcpi_wait = sequential_claim && !same_request;
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
        dot2x_request <= 1'b0;
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
                if (sequential_claim && (!served || !same_request))
                begin
                    custom_request <= fqmul_claim || red32_claim;
                    dot2x_request <= dot2x_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    if (red32_claim)
                    begin
                        // RED32 starts with the product already in the first source
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
                if (dot2x_request)
                begin
                    product_value <= multiply_result[31:0];
                    state <= DOT_PRODUCT;
                end
                else if (custom_request)
                begin
                    product_value <= multiply_result[31:0];
                    state <= INVERSE;
                end
                else
                begin
                    response_value <= multiply_response;
                    state <= RESPONSE;
                end
            end
            DOT_PRODUCT:
            begin
                if (ENABLE_DOT2X)
                begin
                    response_value <= product_value + multiply_result[31:0];
                    state <= RESPONSE;
                end
                else
                begin
                    state <= IDLE;
                    served <= 1'b0;
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
                if (sequential_claim && !same_request)
                begin
                    // a different held request can start without an empty cycle
                    custom_request <= fqmul_claim || red32_claim;
                    dot2x_request <= dot2x_claim;
                    last_insn <= pcpi_insn;
                    last_rs1 <= pcpi_rs1;
                    last_rs2 <= pcpi_rs2;
                    if (red32_claim)
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
