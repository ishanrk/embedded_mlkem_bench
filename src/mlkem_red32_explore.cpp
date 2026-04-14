#include "pqc_poly/mlkem_codegen.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <string_view>

namespace pqc_poly
{
namespace
{

[[nodiscard]] std::string read(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw mlkem_error("cannot open request");
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write(const std::filesystem::path &path, std::string_view value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!output)
    {
        throw mlkem_error("cannot write output");
    }
}

}

int red32_run(int argc, char **argv, std::ostream &output, std::ostream &error)
{
    try
    {
        if (argc == 2 &&
            (std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help"))
        {
            output << "usage: pqc-poly-red32 spec [-o out]\n";
            return 0;
        }
        if (argc != 2 && argc != 4)
        {
            throw mlkem_error("usage: pqc-poly-red32 spec [-o out]");
        }
        std::filesystem::path out = "out-red32";
        if (argc == 4)
        {
            if (std::string_view(argv[2]) != "-o" || std::string_view(argv[3]).empty())
            {
                throw mlkem_error("usage: pqc-poly-red32 spec [-o out]");
            }
            out = argv[3];
        }

        const mlkem_request request = parse_mlkem_request(read(argv[1]));
        const std::vector<red32_plan> plans = enumerate_red32_comparison_plans();
        std::vector<red32_candidate> candidates;
        candidates.reserve(plans.size());
        std::filesystem::create_directories(out / "backends");
        for (const red32_plan &plan : plans)
        {
            candidates.push_back(analyze_red32_plan(request, plan));
            const red32_candidate &candidate = candidates.back();
            if (!candidate.legal || !check_red32_candidate(request, candidate).empty())
            {
                throw mlkem_error("red32 candidate set contains an invalid plan");
            }
            write(out / "backends" / (candidate.id + ".c"),
                  generate_red32_backend(request, candidate));
        }
        if (candidates.size() != 72U)
        {
            throw mlkem_error("red32 experiment count changed");
        }
        write(out / "red32-candidates.json", serialize_red32_candidates(candidates));
        output << "generated 72 standalone red32 comparison plans\n";
        return 0;
    }
    catch (const std::exception &exception)
    {
        error << "error: " << exception.what() << '\n';
        return 2;
    }
}

}
