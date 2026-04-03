#include "pqc_poly/host_tuner.hpp"

#include "pqc_poly/codegen.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#ifndef PQC_POLY_HOST_CXX
#define PQC_POLY_HOST_CXX "c++"
#endif

#ifndef PQC_POLY_HOST_CXX_VERSION
#define PQC_POLY_HOST_CXX_VERSION "unknown"
#endif

#ifndef PQC_POLY_HOST_SIZE
#define PQC_POLY_HOST_SIZE "size"
#endif

namespace pqc_poly
{

namespace
{

class temporary_directory
{
public:
    temporary_directory()
    {
        static std::atomic<std::uint64_t> next{0};
        const std::uint64_t sequence = next.fetch_add(1, std::memory_order_relaxed);
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::uint64_t random = std::random_device{}();

        path_ = std::filesystem::temp_directory_path() /
                ("pqc-poly-tune-" + std::to_string(tick) + "-" + std::to_string(random) + "-" +
                 std::to_string(sequence));
        if (!std::filesystem::create_directory(path_))
        {
            throw std::runtime_error("could not create the tuning workspace");
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

    for (const char character : value)
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
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);

    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream)
    {
        throw std::runtime_error("could not write " + path.string());
    }
}

[[nodiscard]] std::string harness_source(const request &req, const host_tuning_options &options)
{
    const bool negacyclic = req.op == operation::negacyclic_mul;
    const bool aliases = req.alias == aliasing::may;
    std::ostringstream out;

    out << R"pqc(#include "kernel.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string_view>
#include <vector>

#if defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#endif

namespace
{

constexpr std::int64_t input_low = )pqc"
        << input_lower_bound(req) << R"pqc(;
constexpr std::int64_t input_high = )pqc"
        << input_upper_bound(req) << R"pqc(;
constexpr std::size_t sample_count = )pqc"
        << options.samples << R"pqc(;
constexpr std::size_t iteration_count = )pqc"
        << options.iterations << R"pqc(;

[[nodiscard]] std::int64_t reduce_reference(std::int64_t value) noexcept
{
    value %= static_cast<std::int64_t>(pqc_poly_q);
    value += static_cast<std::int64_t>(value < 0) * pqc_poly_q;
    return value;
}

[[nodiscard]] std::vector<std::int32_t> reference_multiply(
    const std::vector<std::int32_t> &a,
    const std::vector<std::int32_t> &b)
{
    std::vector<std::int64_t> accumulation(pqc_poly_n, 0);
    std::vector<std::int32_t> r(pqc_poly_n, 0);

    for (std::size_t i = 0; i < pqc_poly_n; ++i)
    {
        for (std::size_t j = 0; j < pqc_poly_n; ++j)
        {
            std::size_t k = i + j;
            std::int64_t product = static_cast<std::int64_t>(a[i]) * b[j];

            if (k >= pqc_poly_n)
            {
                k -= pqc_poly_n;
                if constexpr ()pqc"
        << (negacyclic ? "true" : "false") << R"pqc()
                {
                    product = -product;
                }
            }
            accumulation[k] = reduce_reference(accumulation[k] + reduce_reference(product));
        }
    }

    for (std::size_t i = 0; i < pqc_poly_n; ++i)
    {
        r[i] = static_cast<std::int32_t>(accumulation[i]);
    }
    return r;
}

[[nodiscard]] std::uint64_t next_random(std::uint64_t &state) noexcept
{
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

[[nodiscard]] std::int32_t random_coefficient(std::uint64_t &state) noexcept
{
    const std::uint64_t width = static_cast<std::uint64_t>(input_high - input_low) + 1;
    return static_cast<std::int32_t>(input_low +
                                     static_cast<std::int64_t>(next_random(state) % width));
}

[[nodiscard]] bool check_case(const std::vector<std::int32_t> &a,
                              const std::vector<std::int32_t> &b)
{
    const std::vector<std::int32_t> expected = reference_multiply(a, b);
    std::vector<std::int32_t> r(pqc_poly_n, 0);

    pqc_poly_mul(r.data(), a.data(), b.data());
    if (r != expected)
    {
        return false;
    }

    if constexpr ()pqc"
        << (aliases ? "true" : "false") << R"pqc()
    {
        std::vector<std::int32_t> r_is_a = a;
        std::vector<std::int32_t> r_is_b = b;
        std::vector<std::int32_t> all_same = a;
        const std::vector<std::int32_t> square = reference_multiply(a, a);

        pqc_poly_mul(r_is_a.data(), r_is_a.data(), b.data());
        pqc_poly_mul(r_is_b.data(), a.data(), r_is_b.data());
        pqc_poly_mul(all_same.data(), all_same.data(), all_same.data());
        if (r_is_a != expected || r_is_b != expected || all_same != square)
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool verify_kernel()
{
    std::vector<std::int32_t> a(pqc_poly_n, 0);
    std::vector<std::int32_t> b(pqc_poly_n, 0);

    if (!check_case(a, b))
    {
        return false;
    }

    a[pqc_poly_n - 1] = 1;
    b[pqc_poly_n > 1 ? 1 : 0] = 1;
    if (!check_case(a, b))
    {
        return false;
    }

    for (std::size_t i = 0; i < pqc_poly_n; ++i)
    {
        a[i] = static_cast<std::int32_t>((i & 1U) == 0 ? input_low : input_high);
        b[i] = static_cast<std::int32_t>((i % 3U) == 0 ? input_high : input_low);
    }
    if (!check_case(a, b) || !check_case(a, a))
    {
        return false;
    }

    std::uint64_t state = 0x243f6a8885a308d3ULL;
    for (std::size_t test = 0; test < 8; ++test)
    {
        for (std::size_t i = 0; i < pqc_poly_n; ++i)
        {
            a[i] = random_coefficient(state);
            b[i] = random_coefficient(state);
        }
        if (!check_case(a, b))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint64_t cycle_counter() noexcept
{
#if defined(__i386__) || defined(__x86_64__)
    _mm_lfence();
    const std::uint64_t value = __rdtsc();
    _mm_lfence();
    return value;
#else
    return 0;
#endif
}

[[nodiscard]] std::pair<std::uint64_t, std::uint64_t> measure_kernel()
{
    std::vector<std::int32_t> a(pqc_poly_n, 0);
    std::vector<std::int32_t> b(pqc_poly_n, 0);
    std::vector<std::int32_t> r(pqc_poly_n, 0);
    std::vector<std::uint64_t> nanoseconds;
    std::vector<std::uint64_t> cycles;
    std::uint64_t state = 0x13198a2e03707344ULL;
    std::uint64_t checksum = 0;

    for (std::size_t i = 0; i < pqc_poly_n; ++i)
    {
        a[i] = random_coefficient(state);
        b[i] = random_coefficient(state);
    }
    for (std::size_t warmup = 0; warmup < 8; ++warmup)
    {
        pqc_poly_mul(r.data(), a.data(), b.data());
        checksum += static_cast<std::uint32_t>(r[warmup % pqc_poly_n]);
    }

    nanoseconds.reserve(sample_count);
    cycles.reserve(sample_count);
    for (std::size_t sample = 0; sample < sample_count; ++sample)
    {
        const auto start_time = std::chrono::steady_clock::now();
        const std::uint64_t start_cycles = cycle_counter();

        for (std::size_t iteration = 0; iteration < iteration_count; ++iteration)
        {
            pqc_poly_mul(r.data(), a.data(), b.data());
            checksum += static_cast<std::uint32_t>(r[(sample + iteration) % pqc_poly_n]);
        }

        const std::uint64_t end_cycles = cycle_counter();
        const auto end_time = std::chrono::steady_clock::now();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        nanoseconds.push_back(
            std::max<std::uint64_t>(1, static_cast<std::uint64_t>(elapsed) / iteration_count));
        if (end_cycles > start_cycles)
        {
            cycles.push_back((end_cycles - start_cycles) / iteration_count);
        }
    }

    std::sort(nanoseconds.begin(), nanoseconds.end());
    std::sort(cycles.begin(), cycles.end());

    // the checksum keeps the benchmark observable without entering the reported metric
    if (checksum == 0x9e3779b97f4a7c15ULL)
    {
        return {0, 0};
    }
    return {
        nanoseconds[nanoseconds.size() / 2],
        cycles.empty() ? 0 : cycles[cycles.size() / 2],
    };
}

}

int main(int argc, char **argv)
{
    if (argc != 3 || !verify_kernel())
    {
        return 1;
    }

    if (std::string_view(argv[1]) == "verify")
    {
        return 0;
    }
    if (std::string_view(argv[1]) != "measure")
    {
        return 2;
    }

    const auto [nanoseconds, cycles] = measure_kernel();
    std::ofstream output(argv[2], std::ios::trunc);

    output << "nanoseconds " << nanoseconds << '\n';
    output << "cycles " << cycles << '\n';
    return output ? 0 : 3;
}
)pqc";

    return out.str();
}

[[nodiscard]] bool run_command(std::string_view command)
{
    return std::system(std::string(command).c_str()) == 0;
}

void reject(benchmark_record &record, std::string reason)
{
    record.status = benchmark_status::rejected;
    record.nanoseconds.reset();
    record.cycles.reset();
    record.rejection_reasons.push_back(std::move(reason));
}

[[nodiscard]] std::uint64_t scratch_bytes(const candidate &candidate) noexcept
{
    if (candidate.analysis.scratch_bytes > std::numeric_limits<std::uint64_t>::max())
    {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return static_cast<std::uint64_t>(candidate.analysis.scratch_bytes);
}

void parse_measurement(const std::filesystem::path &path, benchmark_record &record)
{
    std::ifstream input(path);
    std::string key;
    std::uint64_t value = 0;

    while (input >> key >> value)
    {
        if (key == "nanoseconds")
        {
            record.nanoseconds = value;
        }
        else if (key == "cycles" && value != 0)
        {
            record.cycles = value;
        }
    }
    if (!input.eof() || !record.nanoseconds.has_value())
    {
        throw std::runtime_error("benchmark result is incomplete");
    }
}

[[nodiscard]] std::uint64_t measure_text_size(const std::filesystem::path &object,
                                              const std::filesystem::path &report)
{
    if (std::string_view(PQC_POLY_HOST_SIZE).empty())
    {
        return std::filesystem::file_size(object);
    }
    const std::string command = shell_quote(PQC_POLY_HOST_SIZE) + " -A " +
                                shell_quote(object.string()) + " > " + shell_quote(report.string());
    if (!run_command(command))
    {
        return std::filesystem::file_size(object);
    }

    std::ifstream input(report);
    std::string line;
    std::uint64_t total = 0;

    while (std::getline(input, line))
    {
        std::istringstream fields(line);
        std::string section;
        std::uint64_t bytes = 0;
        std::uint64_t address = 0;

        if (fields >> section >> bytes >> address && section.starts_with(".text"))
        {
            total += bytes;
        }
    }
    return total == 0 ? std::filesystem::file_size(object) : total;
}

[[nodiscard]] benchmark_record tune_one(const std::filesystem::path &root, const request &req,
                                        const candidate &candidate,
                                        const host_tuning_options &options)
{
    benchmark_record record;
    record.plan_id = plan_id(candidate.analysis.plan);
    record.status = benchmark_status::pending;
    record.scratch_bytes = scratch_bytes(candidate);
    record.provenance.compiler = PQC_POLY_HOST_CXX;
    record.provenance.compiler_version = PQC_POLY_HOST_CXX_VERSION;
    record.provenance.compiler_flags =
        "-std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wconversion -Werror";
    record.provenance.target = "host-proxy-for-" + req.target.name;
    record.provenance.runner =
        "local-process; cycles=rdtsc-on-x86; code-size=.text-or-object-fallback";

    const std::vector<std::string> checker_errors = check_candidate(req, candidate);
    record.verification.plan_check = checker_errors.empty();
    record.verification.ram_check = candidate.analysis.scratch_bytes <= req.limits.ram;

    if (!checker_errors.empty())
    {
        reject(record, "plan consistency check failed");
        return record;
    }
    if (!candidate.analysis.legal)
    {
        record.status = benchmark_status::rejected;
        record.rejection_reasons = candidate.analysis.rejections;
        return record;
    }

    const std::filesystem::path directory = root / record.plan_id;
    const std::filesystem::path header = directory / "kernel.hpp";
    const std::filesystem::path source = directory / "kernel.cpp";
    const std::filesystem::path harness = directory / "harness.cpp";
    const std::filesystem::path checked_executable = directory / "kernel-check";
    const std::filesystem::path benchmark_executable = directory / "kernel-benchmark";
    const std::filesystem::path object = directory / "kernel.o";
    const std::filesystem::path size_report = directory / "size.txt";
    const std::filesystem::path result = directory / "result.txt";

    std::filesystem::create_directory(directory);
    write_text(header, generate_header(req, candidate));
    write_text(source, generate_source(req, candidate));
    write_text(harness, harness_source(req, options));

    std::ostringstream checked_compile;
    checked_compile << shell_quote(PQC_POLY_HOST_CXX)
                    << " -std=c++20 -O1 -Wall -Wextra -Wconversion -Werror"
                       " -fsanitize=address,undefined -fno-sanitize-recover=all"
                       " -fno-pie -no-pie -I"
                    << shell_quote(directory.string()) << ' ' << shell_quote(source.string()) << ' '
                    << shell_quote(harness.string()) << " -o "
                    << shell_quote(checked_executable.string());
    if (!run_command(checked_compile.str()))
    {
        reject(record, "sanitized compilation failed");
        return record;
    }

    std::string checked_run;
#if !defined(_WIN32)
    checked_run = "ASAN_OPTIONS=detect_leaks=0 ";
#endif
    checked_run +=
        shell_quote(checked_executable.string()) + " verify " + shell_quote(result.string());
    if (!run_command(checked_run))
    {
        reject(record, "differential or sanitizer verification failed");
        return record;
    }
    record.verification.differential_tests = true;
    record.verification.sanitizers = true;

    std::ostringstream optimized_compile;
    optimized_compile << shell_quote(PQC_POLY_HOST_CXX)
                      << " -std=c++20 -O3 -DNDEBUG -Wall -Wextra -Wconversion -Werror -I"
                      << shell_quote(directory.string()) << ' ' << shell_quote(source.string())
                      << ' ' << shell_quote(harness.string()) << " -o "
                      << shell_quote(benchmark_executable.string());
    if (!run_command(optimized_compile.str()))
    {
        reject(record, "optimized compilation failed");
        return record;
    }

    std::ostringstream object_compile;
    object_compile << shell_quote(PQC_POLY_HOST_CXX) << " -std=c++20 -O3 -DNDEBUG -c "
                   << shell_quote(source.string()) << " -I" << shell_quote(directory.string())
                   << " -o " << shell_quote(object.string());
    if (!run_command(object_compile.str()))
    {
        reject(record, "code-size compilation failed");
        return record;
    }
    record.code_size_bytes = measure_text_size(object, size_report);

    const std::string benchmark_run =
        shell_quote(benchmark_executable.string()) + " measure " + shell_quote(result.string());
    if (!run_command(benchmark_run))
    {
        reject(record, "optimized benchmark failed");
        return record;
    }

    try
    {
        parse_measurement(result, record);
    }
    catch (const std::exception &error)
    {
        reject(record, error.what());
        return record;
    }

    record.status = benchmark_status::measured;
    return record;
}

}

std::vector<benchmark_record> tune_on_host(const request &req,
                                           std::span<const candidate> candidates,
                                           const host_tuning_options &options)
{
    validate_request(req);
    if (options.samples == 0 || options.iterations == 0)
    {
        throw std::invalid_argument("host tuning samples and iterations must be nonzero");
    }

    const temporary_directory workspace;
    std::vector<benchmark_record> records;

    records.reserve(candidates.size());
    for (const candidate &candidate : candidates)
    {
        records.push_back(tune_one(workspace.path(), req, candidate, options));
    }
    return records;
}

}
