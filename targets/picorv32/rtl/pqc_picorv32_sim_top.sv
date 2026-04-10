module pqc_picorv32_sim_top #(
    parameter STOCK_MUL = 1'b0,
    parameter ENABLE_FQMUL = 1'b0
) (
    input  logic        clk,
    input  logic        resetn,
    output logic        trap,
    output logic        benchmark_begin,
    output logic        benchmark_end,
    output logic        status_valid,
    output logic [31:0] status_value,
    output logic        terminate,
    output logic [63:0] cycle_count
);

localparam logic [31:0] memory_limit = 32'h0008_0000;
localparam logic [31:0] begin_address = 32'h1000_0000;
localparam logic [31:0] end_address = 32'h1000_0004;
localparam logic [31:0] status_address = 32'h1000_0008;
localparam logic [31:0] terminate_address = 32'h1000_000c;

logic mem_valid;
logic mem_instr;
logic mem_ready;
logic [31:0] mem_addr;
logic [31:0] mem_wdata;
logic [3:0] mem_wstrb;
logic [31:0] mem_rdata;
logic pending;
logic [31:0] request_addr;
logic [31:0] request_wdata;
logic [3:0] request_wstrb;
logic [31:0] memory [0:131071];
string firmware;

initial
begin
    if (!$value$plusargs("firmware=%s", firmware))
    begin
        $fatal(1, "missing +firmware=<hex>");
    end
    $readmemh(firmware, memory);
end

assign mem_ready = pending;

pqc_picorv32_core_top #(
    .STOCK_MUL(STOCK_MUL),
    .ENABLE_FQMUL(ENABLE_FQMUL)
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
    .mem_rdata(mem_rdata)
);

always_ff @(posedge clk)
begin
    if (!resetn)
    begin
        pending <= 1'b0;
        request_addr <= 32'b0;
        request_wdata <= 32'b0;
        request_wstrb <= 4'b0;
        mem_rdata <= 32'b0;
        benchmark_begin <= 1'b0;
        benchmark_end <= 1'b0;
        status_valid <= 1'b0;
        status_value <= 32'b0;
        terminate <= 1'b0;
        cycle_count <= 64'b0;
    end
    else
    begin
        cycle_count <= cycle_count + 1'b1;
        benchmark_begin <= 1'b0;
        benchmark_end <= 1'b0;
        status_valid <= 1'b0;

        if (pending)
        begin
            pending <= 1'b0;
            if (request_addr < memory_limit)
            begin
                if (request_wstrb[0])
                begin
                    memory[request_addr[18:2]][7:0] <= request_wdata[7:0];
                end
                if (request_wstrb[1])
                begin
                    memory[request_addr[18:2]][15:8] <= request_wdata[15:8];
                end
                if (request_wstrb[2])
                begin
                    memory[request_addr[18:2]][23:16] <= request_wdata[23:16];
                end
                if (request_wstrb[3])
                begin
                    memory[request_addr[18:2]][31:24] <= request_wdata[31:24];
                end
            end
            else if (request_wstrb != 4'b0)
            begin
                case (request_addr)
                    begin_address:
                    begin
                        benchmark_begin <= 1'b1;
                    end
                    end_address:
                    begin
                        benchmark_end <= 1'b1;
                    end
                    status_address:
                    begin
                        status_valid <= 1'b1;
                        status_value <= request_wdata;
                    end
                    terminate_address:
                    begin
                        terminate <= 1'b1;
                    end
                    default:
                    begin
                    end
                endcase
            end
        end
        else if (mem_valid)
        begin
            pending <= 1'b1;
            request_addr <= mem_addr;
            request_wdata <= mem_wdata;
            request_wstrb <= mem_wstrb;
            if (mem_addr < memory_limit)
            begin
                mem_rdata <= memory[mem_addr[18:2]];
            end
            else
            begin
                mem_rdata <= 32'b0;
            end
        end
    end
end

endmodule
