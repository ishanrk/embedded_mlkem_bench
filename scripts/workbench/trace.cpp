#include "Vpqc_pcpi_observe.h"
#include <verilated.h>
#include <verilated_vcd_c.h>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

// drive one PCPI transaction and write both JSON snapshots and a waveform VCD
namespace
{
std::string hex(unsigned __int128 value)
{
    const char *digits = "0123456789abcdef";
    std::string out;
    do
    {
        out.insert(out.begin(), digits[static_cast<unsigned>(value & 15)]);
        value >>= 4;
    } while (value != 0);
    return "0x" + out;
}

void snapshot(const Vpqc_pcpi_observe &model, unsigned edge)
{
    // expose raw internal values so the web UI can explain each FSM step
    std::cout << "{\"edge\":" << edge << ",\"state\":" << unsigned(model.state);
    const auto field = [](const char *name, unsigned __int128 value)
    {
        std::cout << ",\"" << name << "\":\"" << hex(value) << "\"";
    };
    field("valid", model.pcpi_valid);
    field("wait", model.pcpi_wait);
    field("ready", model.pcpi_ready);
    field("wr", model.pcpi_wr);
    field("rd", model.pcpi_rd);
    field("last_rs1", model.last_rs1);
    field("last_rs2", model.last_rs2);
    field("product_value", model.product_value);
    field("inverse_value", model.inverse_value);
    field("modulus_value", model.modulus_value);
    field("response_value", model.response_value);
    field("multiply_left", model.multiply_left);
    field("multiply_right", model.multiply_right);
    field("multiply_result", (static_cast<unsigned __int128>(model.multiply_result[2]) << 64) |
                             (static_cast<unsigned __int128>(model.multiply_result[1]) << 32) |
                             model.multiply_result[0]);
    field("numerator", model.numerator);
    field("fsri_window", model.fsri_window);
    field("fsri_shifted", model.fsri_shifted);
    std::cout << "}";
}
}

int main(int argc, char **argv)
{
    try
    {
        if (argc != 5)
        {
            throw std::runtime_error("usage trace instruction rs1 rs2 output.vcd");
        }
        Verilated::traceEverOn(true);
        Vpqc_pcpi_observe model;
        VerilatedVcdC trace;
        model.trace(&trace, 8);
        trace.open(argv[4]);
        std::uint64_t time = 0;
        // lambda captures model/trace/time and records both clock levels in the VCD
        const auto tick = [&]()
        {
            model.clk = 0;
            model.eval();
            trace.dump(time++);
            model.clk = 1;
            model.eval();
            trace.dump(time++);
        };
        model.resetn = 0;
        model.pcpi_valid = 0;
        tick();
        model.resetn = 1;
        model.pcpi_insn = static_cast<std::uint32_t>(std::stoull(argv[1], nullptr, 0));
        model.pcpi_rs1 = static_cast<std::uint32_t>(std::stoull(argv[2], nullptr, 0));
        model.pcpi_rs2 = static_cast<std::uint32_t>(std::stoull(argv[3], nullptr, 0));
        model.pcpi_valid = 1;
        model.clk = 0;
        model.eval();
        std::cout << "[";
        snapshot(model, 0);
        bool ready = false;
        for (unsigned edge = 1; edge <= 12; ++edge)
        {
            tick();
            std::cout << ",";
            snapshot(model, edge);
            if (model.pcpi_ready)
            {
                ready = true;
                break;
            }
        }
        std::cout << "]\n";
        trace.close();
        if (!ready)
        {
            throw std::runtime_error("response unavailable within twelve edges");
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
