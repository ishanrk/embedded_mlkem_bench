#include "pqc_poly/target_measurement.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "target measurement test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

template <class function_type>
void require_error(function_type &&function, std::string_view message)
{
    try
    {
        function();
    }
    catch (const pqc_poly::target_measurement_error &)
    {
        return;
    }
    fail(message);
}

void test_stack_usage()
{
    const std::vector<pqc_poly::stack_frame> frames = pqc_poly::parse_stack_usage(
        "firmware/runtime.c:18:6:leaf\t16\tstatic\n"
        "firmware/smoke.c:42:5:work\t32\tdynamic,bounded\n"
        "firmware/smoke.c:70:5:root\t48\tstatic\n");

    require(frames.size() == 3, "stack usage record count changed");
    require(frames[0] == pqc_poly::stack_frame{"leaf", 16, false}, "static stack frame changed");
    require(frames[1] == pqc_poly::stack_frame{"work", 32, true},
            "bounded dynamic stack frame changed");

    require_error([] { static_cast<void>(pqc_poly::parse_stack_usage("a.c:1:1:f\t8\tdynamic\n")); },
                  "unbounded dynamic frame was accepted");
    require_error([] { static_cast<void>(pqc_poly::parse_stack_usage("bad\n")); },
                  "malformed stack record was accepted");
    require_error(
        [] {
            static_cast<void>(
                pqc_poly::parse_stack_usage("a.c:1:1:f\t18446744073709551616\tstatic\n"));
        },
        "stack frame overflow was accepted");
}

void test_callchain()
{
    const std::array frames{
        pqc_poly::stack_frame{"root", 48, false},
        pqc_poly::stack_frame{"left", 24, false},
        pqc_poly::stack_frame{"right", 32, false},
        pqc_poly::stack_frame{"leaf", 16, false},
    };
    constexpr std::string_view disassembly = R"(
00000000 <root>:
   0:	000000ef 	jal	ra,10 <left>
   4:	000000ef 	jal	ra,20 <right>
   8:	a001        	j	8 <root+0x8>
00000010 <left>:
  10:	000000ef 	jal	ra,30 <leaf>
00000020 <right>:
  20:	000080e7 	jalr	ra # 30 <leaf>
00000030 <leaf>:
  30:	00008067 	ret
)";

    require(pqc_poly::compute_callchain_stack_bound(frames, disassembly, "root") == 96,
            "maximum callchain bound changed");

    const std::array cloned_frames{
        pqc_poly::stack_frame{"root", 8, false},
        pqc_poly::stack_frame{"work.constprop", 24, false},
        pqc_poly::stack_frame{"work.constprop", 32, false},
    };
    constexpr std::string_view cloned_disassembly = R"(
00000000 <root>:
   0:	000000ef 	jal	ra,10 <work.constprop.1>
00000010 <work.constprop.1>:
  10:	00008067 	ret
)";
    require(
        pqc_poly::compute_callchain_stack_bound(cloned_frames, cloned_disassembly, "root") == 40,
        "compiler clone frame changed");

    constexpr std::string_view indirect = R"(
00000000 <root>:
   0:	000080e7 	jalr	ra
)";
    require(!pqc_poly::compute_callchain_stack_bound(frames, indirect, "root"),
            "indirect call was treated as a zero frame");

    constexpr std::string_view recursive = R"(
00000000 <root>:
   0:	000000ef 	jal	ra,0 <root>
)";
    require(!pqc_poly::compute_callchain_stack_bound(frames, recursive, "root"),
            "recursive callchain was assigned an unproved bound");
    require(!pqc_poly::compute_callchain_stack_bound(frames, disassembly, "missing"),
            "missing root frame was treated as zero");

    const std::array overflow{
        pqc_poly::stack_frame{"root", std::numeric_limits<std::uint64_t>::max(), false},
        pqc_poly::stack_frame{"left", 1, false},
    };
    require_error(
        [&overflow]
        {
            static_cast<void>(pqc_poly::compute_callchain_stack_bound(
                overflow,
                "00000000 <root>:\n 0:\t000000ef \tjal\tra,10 <left>\n"
                "00000010 <left>:\n 10:\t00008067 \tret\n",
                "root"));
        },
        "callchain overflow was accepted");
}

void test_elf_size()
{
    constexpr std::string_view input = R"(
firmware.elf  :
section            size       addr
.text               400          0
.text.startup         20        400
.rodata               64        420
.srodata.constants    12        484
.data                  8        496
.sdata                 4        504
.bss                  128        508
.sbss                   2        636
.comment               99          0
Total                 737
)";
    require(
        pqc_poly::parse_elf_size(input) == pqc_poly::code_size_measurement{420, 76, 12, 130, 508},
        "elf section totals changed");
    require_error([] { static_cast<void>(pqc_poly::parse_elf_size("Total 10\n")); },
                  "sectionless size output was accepted");
    require_error(
        [] {
            static_cast<void>(
                pqc_poly::parse_elf_size(".text 18446744073709551615 0\n.text.more 1 0\n"));
        },
        "elf section overflow was accepted");
    require_error(
        [] {
            static_cast<void>(
                pqc_poly::parse_elf_size(".text 18446744073709551615 0\n.rodata 1 0\n"));
        },
        "allocated flash overflow was accepted");
}

void test_cycles()
{
    constexpr std::string_view input = R"({
  "begin_cycle": 120,
  "end_cycle": 370,
  "marker_overhead_cycles": 14,
  "calibrated_cycles": 236,
  "terminated": true,
  "trapped": false
})";
    require(pqc_poly::parse_simulation_measurement(input) ==
                pqc_poly::cycle_measurement{120, 370, 14, 236, true, false},
            "cycle calibration changed");
    require_error(
        []
        {
            static_cast<void>(pqc_poly::parse_simulation_measurement(
                R"({"begin_cycle":9,"end_cycle":8,"marker_overhead_cycles":0,"calibrated_cycles":0,"terminated":true,"trapped":false})"));
        },
        "reversed cycle interval was accepted");
    require_error(
        []
        {
            static_cast<void>(pqc_poly::parse_simulation_measurement(
                R"({"begin_cycle":1,"end_cycle":2,"marker_overhead_cycles":2,"calibrated_cycles":0,"terminated":true,"trapped":false})"));
        },
        "excess marker overhead was accepted");
    require_error(
        []
        {
            static_cast<void>(pqc_poly::parse_simulation_measurement(
                R"({"begin_cycle":1,"end_cycle":5,"marker_overhead_cycles":1,"calibrated_cycles":4,"terminated":true,"trapped":false})"));
        },
        "inconsistent calibrated cycles were accepted");
}

void test_mlkem_cycles()
{
    constexpr std::string_view input =
        R"({"schema":"pqc-poly-bench/mlkem-measurement-v1","plan_id":"p","level":"512","operation":"forward_ntt","multiplier":"project","input":0,"repeat":0,"begin_cycle":10,"end_cycle":30,"marker_overhead_cycles":2,"calibrated_cycles":18,"instruction_count":7}
{"schema":"pqc-poly-bench/mlkem-measurement-v1","plan_id":"p","level":"512","operation":"keygen","multiplier":"stock","input":29,"repeat":2,"begin_cycle":50,"end_cycle":80,"marker_overhead_cycles":3,"calibrated_cycles":27,"instruction_count":11}
)";
    const std::vector<pqc_poly::mlkem_cycle_measurement> records =
        pqc_poly::parse_mlkem_cycle_measurements(input);
    require(records.size() == 2 && records[0].calibrated_cycles == 18 && records[1].input == 29 &&
                records[1].repeat == 2,
            "mlkem measurement parse changed");
    require_error(
        []
        {
            static_cast<void>(pqc_poly::parse_mlkem_cycle_measurements(
                R"({"schema":"pqc-poly-bench/mlkem-measurement-v1","plan_id":"p","level":"512","operation":"forward_ntt","multiplier":"project","input":4294967296,"repeat":0,"begin_cycle":10,"end_cycle":30,"marker_overhead_cycles":2,"calibrated_cycles":18,"instruction_count":7})"));
        },
        "mlkem input overflow was accepted");
    require_error(
        []
        {
            static_cast<void>(pqc_poly::parse_mlkem_cycle_measurements(
                R"({"schema":"pqc-poly-bench/mlkem-measurement-v1","plan_id":"p","level":"512","operation":"forward_ntt","multiplier":"project","input":0,"repeat":0,"begin_cycle":10,"end_cycle":30,"marker_overhead_cycles":2,"calibrated_cycles":17,"instruction_count":7})"));
        },
        "inconsistent mlkem cycle result was accepted");
}

void test_synthesis()
{
    constexpr std::string_view input = R"({
  "yosys_version": "Yosys 0.50",
  "nextpnr_version": "nextpnr-ecp5 0.9",
  "seeds": [
    {
      "seed": 1,
      "lut4": 1900,
      "flip_flops": 900,
      "dsp": 4,
      "bram": 0,
      "maximum_frequency_mhz": 52.25,
      "meets_50mhz": true,
      "command": "nextpnr-ecp5 --seed 1"
    },
    {
      "seed": 2,
      "lut4": 1910,
      "flip_flops": 905,
      "dsp": 4,
      "bram": 0,
      "maximum_frequency_mhz": 49.75,
      "meets_50mhz": false,
      "command": "nextpnr-ecp5 --seed 2"
    }
  ]
})";
    const pqc_poly::synthesis_measurement result = pqc_poly::parse_synthesis_measurement(input);
    require(result.seeds.size() == 2, "synthesis seed count changed");
    require(result.seeds[0].seed == 1 && result.seeds[0].meets_50mhz,
            "passing synthesis seed changed");
    require(result.seeds[1].seed == 2 && !result.seeds[1].meets_50mhz,
            "failing synthesis seed changed");

    require_error(
        []
        {
            static_cast<void>(pqc_poly::parse_synthesis_measurement(
                R"({"yosys_version":"y","nextpnr_version":"n","seeds":[{"seed":1,"lut4":1,"flip_flops":1,"dsp":0,"bram":0,"maximum_frequency_mhz":49.0,"meets_50mhz":true,"command":"x"}]})"));
        },
        "inconsistent synthesis pass flag was accepted");
}

}

int main()
{
    test_stack_usage();
    test_callchain();
    test_elf_size();
    test_cycles();
    test_mlkem_cycles();
    test_synthesis();
}
