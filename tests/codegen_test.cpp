#include "pqc_poly/codegen.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifndef PQC_POLY_TEST_CXX
#define PQC_POLY_TEST_CXX "c++"
#endif

namespace
{

using pqc_poly::aliasing;
using pqc_poly::candidate;
using pqc_poly::input_representation;
using pqc_poly::operation;
using pqc_poly::output_representation;
using pqc_poly::request;
using pqc_poly::schedule;

[[noreturn]] void fail(std::string_view message)
{
    std::cerr << "codegen test failed: " << message << '\n';
    std::exit(1);
}

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        fail(message);
    }
}

[[nodiscard]] request make_request(operation op, aliasing alias)
{
    request req;
    req.op = op;
    req.n = 5;
    req.q = 17;
    req.input = input_representation::centered;
    req.output = output_representation::canonical;
    req.alias = alias;
    req.target.name = "test";
    req.target.word_bits = 32;
    req.target.size_bits = 32;
    req.target.acc_bits = {32, 64};
    req.limits.ram = 1'000;
    return req;
}

[[nodiscard]] const candidate &trial_for(std::span<const candidate> trials,
                                               schedule wanted, std::uint64_t block = 0)
{
    for (const candidate &trial : trials)
    {
        if (trial.analysis.legal && trial.analysis.plan.sched == wanted &&
            (wanted != schedule::fold || trial.analysis.plan.block == block))
        {
            return trial;
        }
    }

    std::cerr << "missing schedule " << pqc_poly::schedule_name(wanted) << " with block " << block
              << '\n';
    for (const candidate &trial : trials)
    {
        std::cerr << "  " << pqc_poly::plan_id(trial.analysis.plan)
                  << " legal=" << trial.analysis.legal << '\n';
    }
    fail("expected legal schedule was not generated");
}

void require_contains(std::string_view text, std::string_view needle, std::string_view message)
{
    require(text.find(needle) != std::string_view::npos, message);
}

void require_lowercase_comments(std::string_view text)
{
    bool block_comment = false;

    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (!block_comment && i + 1 < text.size() && text[i] == '/' && text[i + 1] == '/')
        {
            for (i += 2; i < text.size() && text[i] != '\n'; ++i)
            {
                require(text[i] < 'A' || text[i] > 'Z', "generated line comment is not lowercase");
            }
        }
        else if (!block_comment && i + 1 < text.size() && text[i] == '/' && text[i + 1] == '*')
        {
            block_comment = true;
            ++i;
        }
        else if (block_comment && i + 1 < text.size() && text[i] == '*' && text[i + 1] == '/')
        {
            block_comment = false;
            ++i;
        }
        else if (block_comment)
        {
            require(text[i] < 'A' || text[i] > 'Z', "generated block comment is not lowercase");
        }
    }

    require(!block_comment, "generated block comment is unterminated");
}

class temporary_directory
{
public:
    explicit temporary_directory(std::string_view label)
    {
        static std::atomic<std::uint64_t> next{0};
        const std::uint64_t sequence = next.fetch_add(1, std::memory_order_relaxed);
        const auto clock_value = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::uint64_t random_value = std::random_device{}();
        path_ = std::filesystem::temp_directory_path() /
                ("pqc-poly-codegen-" + std::string(label) + "-" + std::to_string(clock_value) +
                 "-" + std::to_string(random_value) + "-" + std::to_string(sequence));

        if (!std::filesystem::create_directory(path_))
        {
            fail("could not create a temporary directory");
        }
    }

    temporary_directory(const temporary_directory &) = delete;
    temporary_directory &operator=(const temporary_directory &) = delete;

    ~temporary_directory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string shell_quote(std::string_view value)
{
    std::string quoted{"'"};

    for (char character : value)
    {
        if (character == '\'')
        {
            quoted += "'\\''";
        }
        else
        {
            quoted.push_back(character);
        }
    }

    quoted.push_back('\'');
    return quoted;
}

void write_text(const std::filesystem::path &path, std::string_view text)
{
    std::ofstream stream(path, std::ios::binary);
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));

    if (!stream)
    {
        fail("could not write generated test input");
    }
}

[[nodiscard]] std::string coefficients(const std::vector<std::int32_t> &values)
{
    std::ostringstream out;

    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i != 0)
        {
            out << ", ";
        }
        out << values[i];
    }

    return out.str();
}

[[nodiscard]] std::vector<std::int32_t> reference_product(operation op,
                                                          const std::vector<std::int32_t> &a,
                                                          const std::vector<std::int32_t> &b,
                                                          std::int64_t q)
{
    require(a.size() == b.size(), "reference inputs differ in size");
    std::vector<std::int64_t> t(a.size(), 0);
    std::vector<std::int32_t> result(a.size(), 0);

    for (std::size_t i = 0; i < a.size(); ++i)
    {
        for (std::size_t j = 0; j < b.size(); ++j)
        {
            const std::int64_t product =
                static_cast<std::int64_t>(a[i]) * static_cast<std::int64_t>(b[j]);
            const std::size_t unfolded = i + j;

            if (unfolded < t.size())
            {
                t[unfolded] += product;
            }
            else if (op == operation::cyclic_mul)
            {
                t[unfolded - t.size()] += product;
            }
            else
            {
                t[unfolded - t.size()] -= product;
            }
        }
    }

    for (std::size_t i = 0; i < t.size(); ++i)
    {
        const std::int64_t reduced = ((t[i] % q) + q) % q;
        result[i] = static_cast<std::int32_t>(reduced);
    }

    return result;
}

[[nodiscard]] std::string differential_harness(const std::vector<std::int32_t> &a,
                                               const std::vector<std::int32_t> &b,
                                               const std::vector<std::int32_t> &expected)
{
    std::ostringstream out;

    out << R"pqc(#include "kernel.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

int main()
{
    const std::array<std::int32_t, pqc_poly_n> a{)pqc"
        << coefficients(a) << R"pqc(};
    const std::array<std::int32_t, pqc_poly_n> b{)pqc"
        << coefficients(b) << R"pqc(};
    const std::array<std::int32_t, pqc_poly_n> expected{)pqc"
        << coefficients(expected) << R"pqc(};
    std::array<std::int32_t, pqc_poly_n> r{};

    pqc_poly_mul(r.data(), a.data(), b.data());

    return r == expected ? 0 : 1;
}
)pqc";

    return out.str();
}

[[nodiscard]] std::string alias_harness(const std::vector<std::int32_t> &a,
                                        const std::vector<std::int32_t> &b,
                                        const std::vector<std::int32_t> &expected,
                                        const std::vector<std::int32_t> &square)
{
    std::ostringstream out;

    out << R"pqc(#include "kernel.hpp"

#include <array>
#include <cstdint>

int main()
{
    const std::array<std::int32_t, pqc_poly_n> a{)pqc"
        << coefficients(a) << R"pqc(};
    const std::array<std::int32_t, pqc_poly_n> b{)pqc"
        << coefficients(b) << R"pqc(};
    const std::array<std::int32_t, pqc_poly_n> expected{)pqc"
        << coefficients(expected) << R"pqc(};
    const std::array<std::int32_t, pqc_poly_n> square{)pqc"
        << coefficients(square) << R"pqc(};
    std::array<std::int32_t, pqc_poly_n> r_is_a = a;
    std::array<std::int32_t, pqc_poly_n> r_is_b = b;
    std::array<std::int32_t, pqc_poly_n> all_same = a;

    pqc_poly_mul(r_is_a.data(), r_is_a.data(), b.data());
    pqc_poly_mul(r_is_b.data(), a.data(), r_is_b.data());
    pqc_poly_mul(all_same.data(), all_same.data(), all_same.data());

    return r_is_a == expected && r_is_b == expected && all_same == square ? 0 : 1;
}
)pqc";

    return out.str();
}

void compile_and_run(std::string_view label, const request &req, const candidate &trial,
                     std::string_view harness)
{
    const temporary_directory directory(label);
    const std::filesystem::path header = directory.path() / "kernel.hpp";
    const std::filesystem::path source = directory.path() / "kernel.cpp";
    const std::filesystem::path harness_path = directory.path() / "harness.cpp";
    const std::filesystem::path executable = directory.path() / "kernel-check";

    write_text(header, pqc_poly::generate_header(req, trial));
    write_text(source, pqc_poly::generate_source(req, trial));
    write_text(harness_path, harness);

    std::ostringstream compile;
    compile << shell_quote(PQC_POLY_TEST_CXX)
            << " -std=c++20 -O3 -Wall -Wextra -Wconversion -Wsign-conversion -Werror"
            << " -fsanitize=undefined -fno-sanitize-recover=all"
            << " -I" << shell_quote(directory.path().string()) << ' '
            << shell_quote(source.string()) << ' ' << shell_quote(harness_path.string()) << " -o "
            << shell_quote(executable.string());

    require(std::system(compile.str().c_str()) == 0, "generated c++ compilation failed");
    require(std::system(shell_quote(executable.string()).c_str()) == 0,
            "generated c++ execution failed");
}

void test_header_contract()
{
    const request req = make_request(operation::negacyclic_mul, aliasing::may);
    const std::vector<candidate> trials = pqc_poly::find_candidates(req);
    const candidate &trial = trial_for(trials, schedule::full);
    const std::string first = pqc_poly::generate_header(req, trial);
    const std::string second = pqc_poly::generate_header(req, trial);

    require(first == second, "header output must be deterministic");
    require_contains(first, "inline constexpr std::size_t pqc_poly_n", "missing n constant");
    require_contains(first, "extern \"C\" void pqc_poly_mul", "missing prefixed c abi");
    require_contains(first, "r, a, and b may overlap", "missing alias contract");
    require_lowercase_comments(first);
}

void test_schedule_bodies()
{
    for (operation op : {operation::cyclic_mul, operation::negacyclic_mul})
    {
        const request req = make_request(op, aliasing::no);
        const std::vector<candidate> trials = pqc_poly::find_candidates(req);
        const candidate &full = trial_for(trials, schedule::full);
        const candidate &fold = trial_for(trials, schedule::fold, 4);
        const candidate &output = trial_for(trials, schedule::output);
        const std::string full_source = pqc_poly::generate_source(req, full);
        const std::string fold_source = pqc_poly::generate_source(req, fold);
        const std::string output_source = pqc_poly::generate_source(req, output);
        const std::string_view fold_operator = op == operation::cyclic_mul ? "+=" : "-=";

        require_contains(full_source, "alignas(64) acc_t t[2 * pqc_poly_n - 1]", "full scratch");
        require_contains(full_source, "PQC_POLY_VECTOR_LOOP", "full vector hint");
        require_contains(fold_source, "const std::size_t direct_end", "fold split");
        require_contains(fold_source, "const std::size_t wrap_start", "fold wrap split");
        require_contains(output_source, "acc_t s3 = 0", "output latency lanes");
        require_contains(output_source, "r[k] = reduce_mod_q", "output reduction");
        require_contains(full_source, "t[i] " + std::string(fold_operator), "full ring sign");
        require_contains(fold_source, "t[j - split] " + std::string(fold_operator),
                         "fold ring sign");
        require_contains(output_source, "s0 " + std::string(fold_operator), "output ring sign");
        require_lowercase_comments(full_source);
        require_lowercase_comments(fold_source);
        require_lowercase_comments(output_source);
    }
}

void test_legality_gate()
{
    const request req = make_request(operation::negacyclic_mul, aliasing::no);
    std::vector<candidate> trials = pqc_poly::find_candidates(req);
    candidate damaged = trial_for(trials, schedule::full);

    damaged.analysis.accumulator_bound += 1;

    try
    {
        static_cast<void>(pqc_poly::generate_source(req, damaged));
    }
    catch (const pqc_poly::codegen_error &error)
    {
        require_contains(error.what(), "bad plan:", "reference error prefix changed");
        return;
    }

    fail("inconsistent analysis reached the emitter");
}

void test_illegal_plan_gate()
{
    const request req = make_request(operation::negacyclic_mul, aliasing::may);
    const std::vector<candidate> trials = pqc_poly::find_candidates(req);

    for (const candidate &trial : trials)
    {
        if (trial.analysis.plan.sched == schedule::output)
        {
            require(!trial.analysis.legal, "aliasing output trial unexpectedly became legal");

            try
            {
                static_cast<void>(pqc_poly::generate_source(req, trial));
            }
            catch (const pqc_poly::codegen_error &error)
            {
                require_contains(error.what(), "cannot emit an illegal plan",
                                 "illegal plan error changed");
                return;
            }

            fail("illegal plan reached the emitter");
        }
    }

    fail("output trial was not enumerated");
}

void test_power_of_two_reduction()
{
    request req = make_request(operation::negacyclic_mul, aliasing::no);
    req.q = 16;
    const std::vector<candidate> trials = pqc_poly::find_candidates(req);
    const candidate &trial = trial_for(trials, schedule::full);
    const std::string source = pqc_poly::generate_source(req, trial);

    require_contains(source, "using unsigned_acc_t", "missing power-of-two fast path");
    require_contains(source, "& mask", "missing power-of-two mask");
}

void test_compiled_schedules()
{
    const std::vector<std::int32_t> a{-8, -1, 0, 4, 8};
    const std::vector<std::int32_t> b{7, -3, 2, -8, 1};

    for (operation op : {operation::cyclic_mul, operation::negacyclic_mul})
    {
        const request req = make_request(op, aliasing::no);
        const std::vector<candidate> trials = pqc_poly::find_candidates(req);
        const std::vector<std::int32_t> expected = reference_product(op, a, b, req.q);

        for (schedule wanted : {schedule::full, schedule::fold, schedule::output})
        {
            const std::uint64_t block = wanted == schedule::fold ? 4 : 0;
            const candidate &trial = trial_for(trials, wanted, block);
            const std::string label = std::string(pqc_poly::operation_name(op)) + "-" +
                                      std::string(pqc_poly::schedule_name(wanted));

            compile_and_run(label, req, trial, differential_harness(a, b, expected));
        }
    }
}

void test_compiled_widening_schedules()
{
    request req = make_request(operation::negacyclic_mul, aliasing::no);
    req.n = 2;
    req.q = static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max());
    req.input = input_representation::canonical;
    req.target.acc_bits = {64};
    const std::vector<std::int32_t> a{
        static_cast<std::int32_t>(req.q - 1),
        static_cast<std::int32_t>(req.q - 2),
    };
    const std::vector<std::int32_t> b{
        static_cast<std::int32_t>(req.q - 3),
        static_cast<std::int32_t>(req.q - 4),
    };
    const std::vector<candidate> trials = pqc_poly::find_candidates(req);
    const std::vector<std::int32_t> expected = reference_product(req.op, a, b, req.q);

    for (schedule wanted : {schedule::full, schedule::fold, schedule::output})
    {
        const std::uint64_t block = wanted == schedule::fold ? req.n : 0;
        const candidate &trial = trial_for(trials, wanted, block);

        require(trial.analysis.plan.acc_bits == 64, "widening test did not select i64");
        compile_and_run("i64-" + std::string(pqc_poly::schedule_name(wanted)), req, trial,
                        differential_harness(a, b, expected));
    }
}

void test_compiled_aliasing()
{
    const std::vector<std::int32_t> a{-8, -1, 0, 4, 8};
    const std::vector<std::int32_t> b{7, -3, 2, -8, 1};

    for (operation op : {operation::cyclic_mul, operation::negacyclic_mul})
    {
        const request req = make_request(op, aliasing::may);
        const std::vector<candidate> trials = pqc_poly::find_candidates(req);
        const std::vector<std::int32_t> expected = reference_product(op, a, b, req.q);
        const std::vector<std::int32_t> square = reference_product(op, a, a, req.q);

        for (schedule wanted : {schedule::full, schedule::fold})
        {
            const std::uint64_t block = wanted == schedule::fold ? 4 : 0;
            const candidate &trial = trial_for(trials, wanted, block);
            const std::string label = "alias-" + std::string(pqc_poly::operation_name(op)) + "-" +
                                      std::string(pqc_poly::schedule_name(wanted));

            require(trial.analysis.alias_safe, "alias test used an unsafe trial");
            compile_and_run(label, req, trial, alias_harness(a, b, expected, square));
        }
    }
}

void test_compiled_project_examples()
{
    struct example
    {
        operation op;
        std::uint64_t n;
        std::uint32_t q;
        std::int32_t low;
        std::int32_t high;
        std::string_view label;
    };
    const std::array examples = {
        example{operation::negacyclic_mul, 256, 3329, -1664, 1664, "mlkem"},
        example{operation::cyclic_mul, 509, 2048, -1024, 1023, "ntruhps2048509"},
    };

    for (const example &value : examples)
    {
        request req = make_request(value.op, aliasing::no);

        req.n = value.n;
        req.q = value.q;
        req.limits.ram = 4096;

        std::vector<std::int32_t> a(req.n);
        std::vector<std::int32_t> b(req.n);
        for (std::size_t i = 0; i < req.n; ++i)
        {
            a[i] = (i & 1U) == 0 ? value.low : value.high;
            b[i] = (i % 3U) == 0 ? value.high : value.low;
        }

        const std::vector<candidate> trials = pqc_poly::find_candidates(req);
        const candidate &selected = pqc_poly::pick_static(trials);
        const std::vector<std::int32_t> expected = reference_product(req.op, a, b, req.q);

        require(selected.analysis.plan.sched == schedule::full,
                "project example selection changed unexpectedly");
        compile_and_run(value.label, req, selected, differential_harness(a, b, expected));
    }
}

}

int main()
{
    test_header_contract();
    test_schedule_bodies();
    test_legality_gate();
    test_illegal_plan_gate();
    test_power_of_two_reduction();
    test_compiled_schedules();
    test_compiled_widening_schedules();
    test_compiled_aliasing();
    test_compiled_project_examples();
    return 0;
}
