#include "pqc_poly/explore.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace
{

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "explore test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

[[nodiscard]] std::string read_text(const std::filesystem::path &path)
{
    std::ifstream stream(path, std::ios::binary);
    std::ostringstream text;

    text << stream.rdbuf();
    if (!stream)
    {
        fail("could not read artifact");
    }
    return text.str();
}

void write_text(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);

    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream)
    {
        fail("could not write request");
    }
}

struct temporary_directory
{
    std::filesystem::path path;

    temporary_directory()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();

        path = std::filesystem::temp_directory_path() /
               ("pqc-poly-explore-test-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }

    ~temporary_directory()
    {
        std::error_code error;

        std::filesystem::remove_all(path, error);
    }
};

constexpr std::string_view request_json = R"json({
  "op": "negacyclic_mul",
  "n": 8,
  "q": 17,
  "target": {
    "name": "rv32-\u03bc-\ud83d\ude80",
    "word_bits": 32,
    "size_bits": 32,
    "acc_bits": [32, 64]
  },
  "limits": {
    "ram": 1000
  }
})json";

constexpr std::string_view aliasing_request_json = R"json({
  "op": "negacyclic_mul",
  "n": 8,
  "q": 17,
  "alias": "may",
  "target": {
    "name": "test",
    "word_bits": 32,
    "size_bits": 32,
    "acc_bits": [32, 64]
  },
  "limits": {
    "ram": 1000
  }
})json";

constexpr std::string_view tuning_request_json = R"json({
  "op": "negacyclic_mul",
  "n": 2,
  "q": 17,
  "alias": "may",
  "target": {
    "name": "host",
    "acc_bits": [32]
  },
  "limits": {
    "ram": 64
  }
})json";

void test_run_and_artifacts()
{
    temporary_directory temporary;
    const auto specification = temporary.path / "request.json";
    const auto first = temporary.path / "first";
    const auto second = temporary.path / "second";

    write_text(specification, request_json);

    const std::string specification_text = specification.string();
    const std::string first_text = first.string();
    const std::string second_text = second.string();
    const std::string second_option = "-o=" + second_text;
    const std::array first_arguments = {
        std::string_view(specification_text),
        std::string_view("-o"),
        std::string_view(first_text),
    };
    const std::array second_arguments = {
        std::string_view(second_option),
        std::string_view(specification_text),
    };
    std::ostringstream first_output;
    std::ostringstream first_error;
    std::ostringstream second_output;
    std::ostringstream second_error;

    require(pqc_poly::run(first_arguments, first_output, first_error) == 0, "first run failed");
    require(pqc_poly::run(second_arguments, second_output, second_error) == 0, "second run failed");
    require(first_error.str().empty() && second_error.str().empty(), "unexpected error output");
    require(first_output.str().find("sb_full_i32") != std::string::npos, "missing selection");

    const std::set<std::string> expected_files = {
        "benchmarks.json", "candidates.json", "kernel.cpp", "kernel.hpp", "plan.json",
    };
    std::set<std::string> files;

    for (const auto &entry : std::filesystem::directory_iterator(first))
    {
        files.insert(entry.path().filename().string());
    }
    require(files == expected_files, "artifact set differs");

    for (const std::string &name : expected_files)
    {
        require(read_text(first / name) == read_text(second / name),
                "artifacts are not deterministic");
    }

    const std::string metadata = read_text(first / "plan.json");

    require(metadata.find("rv32-\\u03bc-\\ud83d\\ude80") != std::string::npos,
            "json did not use ascii escapes");
    require(read_text(first / "benchmarks.json").find("\"selected\": null") != std::string::npos,
            "static run claimed a measured winner");
    require(read_text(first / "candidates.json").find("\"scratch_bytes\"") !=
                std::string::npos,
            "candidate artifact lacks scratch accounting");
    require(read_text(first / "kernel.cpp").find("extern \"C\" PQC_POLY_HOT void pqc_poly_mul") !=
                std::string::npos,
            "generated c++ entry point is missing");
}

void test_cli_errors()
{
    std::ostringstream output;
    std::ostringstream error;

    require(pqc_poly::run(std::span<const std::string_view>{}, output, error) == 2,
            "missing spec must fail");
    require(error.str().find("required: spec") != std::string::npos, "missing spec error absent");

    output.str("");
    error.str("");
    const std::array help = {std::string_view("--help")};
    require(pqc_poly::run(help, output, error) == 0, "help must succeed");
    require(output.str().starts_with("usage:"), "help usage absent");
}

void test_forced_plan_selection()
{
    temporary_directory temporary;
    const auto specification = temporary.path / "request.json";
    const auto out = temporary.path / "forced";

    write_text(specification, request_json);

    const std::string specification_text = specification.string();
    const std::string out_option = "-o=" + out.string();
    const std::array arguments = {
        std::string_view("--plan"),
        std::string_view("sb_fold_b4_i32"),
        std::string_view(out_option),
        std::string_view(specification_text),
    };
    std::ostringstream output;
    std::ostringstream error;

    require(pqc_poly::run(arguments, output, error) == 0, "forced legal plan failed");
    require(error.str().empty(), "forced legal plan wrote an error");
    require(output.str().find("\"selected\": \"sb_fold_b4_i32\"") != std::string::npos,
            "forced plan was not selected");
    require(read_text(out / "plan.json").find("\"id\": \"sb_fold_b4_i32\"") != std::string::npos,
            "forced plan metadata differs");
}

void test_plan_errors()
{
    temporary_directory temporary;
    const auto regular_specification = temporary.path / "request.json";
    const auto aliasing_specification = temporary.path / "aliasing.json";
    const auto unknown_out = temporary.path / "unknown";
    const auto illegal_out = temporary.path / "illegal";

    write_text(regular_specification, request_json);
    write_text(aliasing_specification, aliasing_request_json);

    const std::string regular_text = regular_specification.string();
    const std::string aliasing_text = aliasing_specification.string();
    const std::string unknown_option = "-o=" + unknown_out.string();
    const std::string illegal_option = "-o=" + illegal_out.string();
    const std::array unknown_arguments = {
        std::string_view("--plan=does_not_exist"),
        std::string_view(unknown_option),
        std::string_view(regular_text),
    };
    const std::array illegal_arguments = {
        std::string_view("--plan"),
        std::string_view("sb_out_i32"),
        std::string_view(illegal_option),
        std::string_view(aliasing_text),
    };
    std::ostringstream output;
    std::ostringstream error;

    require(pqc_poly::run(unknown_arguments, output, error) == 2, "unknown plan must fail");
    require(error.str().find("unknown plan: does_not_exist") != std::string::npos,
            "unknown plan error absent");
    require(!std::filesystem::exists(unknown_out), "unknown plan emitted artifacts");

    output.str("");
    output.clear();
    error.str("");
    error.clear();

    require(pqc_poly::run(illegal_arguments, output, error) == 2, "illegal plan must fail");
    require(error.str().find("plan is illegal:") != std::string::npos, "illegal plan error absent");
    require(!std::filesystem::exists(illegal_out), "illegal plan emitted artifacts");
}

void test_host_tuning_cli()
{
    temporary_directory temporary;
    const auto specification = temporary.path / "tuning.json";
    const auto out = temporary.path / "tuned";

    write_text(specification, tuning_request_json);

    const std::string specification_text = specification.string();
    const std::string out_option = "--out=" + out.string();
    const std::array arguments = {
        std::string_view("--tune-host"),    std::string_view("--samples=1"),
        std::string_view("--iterations=1"), std::string_view("--metric=nanoseconds"),
        std::string_view(out_option),       std::string_view(specification_text),
    };
    std::ostringstream output;
    std::ostringstream error;

    require(pqc_poly::run(arguments, output, error) == 0, "host tuning cli failed");
    require(error.str().empty(), "host tuning cli wrote an error");
    require(output.str().find("\"selection_mode\": \"measured_host_proxy\"") != std::string::npos,
            "host tuning cli did not select a measurement");
    require(read_text(out / "benchmarks.json").find("\"selected\": null") == std::string::npos,
            "host tuning artifact lacks a winner");
    require(read_text(out / "plan.json").find("\"host_measurement\": null") == std::string::npos,
            "host tuning plan lacks its measurement");
    require(read_text(out / "benchmarks.json")
                    .find("not formal verification or target execution") != std::string::npos,
            "benchmark artifact overstates its verification scope");
}

}

int main()
{
    test_run_and_artifacts();
    test_cli_errors();
    test_forced_plan_selection();
    test_plan_errors();
    test_host_tuning_cli();
    return 0;
}
