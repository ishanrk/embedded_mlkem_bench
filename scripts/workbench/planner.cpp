#include "pqc_poly/mlkem_codegen.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
// writes planner records and candidates for the browser view
void records(const std::vector<pqc_poly::mlkem_record> &values)
{
    std::cout << '[';
    bool first = true;
    for (const auto &value : values)
    {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"layer\":" << value.layer << ",\"block\":" << value.block
                  << ",\"zeta_index\":" << value.zeta_index << ",\"left_base\":" << value.left_base
                  << ",\"right_base\":" << value.right_base << ",\"length\":" << value.length << '}';
    }
    std::cout << ']';
}
void strings(const std::vector<std::string> &values)
{
    std::cout << '[';
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        if (i) std::cout << ',';
        std::cout << '"' << values[i] << '"';
    }
    std::cout << ']';
}
}
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    const std::filesystem::path directory(argv[1]);
    std::filesystem::create_directories(directory);
    const auto plans = pqc_poly::enumerate_mlkem_plans();
    const auto initial = pqc_poly::analyze_mlkem_plan({}, plans.front());
    std::cout << "{\"schema\":\"pqc-poly-bench/planner-walkthrough-v1\",\"forward_records\":";
    records(initial.forward_records);
    std::cout << ",\"inverse_records\":";
    records(initial.inverse_records);
    std::cout << ",\"candidates\":[";
    bool first = true;
    // analyzes all 144 plans under several scratch and workspace limits
    for (const auto &plan : plans)
    {
        const auto candidate = pqc_poly::analyze_mlkem_plan({}, plan);
        if (!pqc_poly::check_mlkem_plan({}, candidate).empty()) return 2;
        if (!first) std::cout << ',';
        first = false;
        auto serialized = pqc_poly::serialize_mlkem_candidate(candidate);
        serialized.erase(serialized.find_last_of('}'));
        std::cout << serialized << ",\"memory_checks\":[";
        bool first_request = true;
        for (const std::uint64_t scratch : {0U, 512U, 768U, 1024U})
        {
            for (const std::uint64_t workspace : {0U, 2560U, 3584U, 4608U})
            {
                const pqc_poly::mlkem_request request{scratch, workspace};
                const auto analyzed = pqc_poly::analyze_mlkem_plan(request, plan);
                if (!first_request) std::cout << ',';
                first_request = false;
                std::cout << "{\"scratch_limit\":" << scratch << ",\"caller_workspace_limit\":" << workspace
                          << ",\"legal\":" << (analyzed.legal ? "true" : "false") << ",\"rejections\":";
                strings(analyzed.rejections);
                std::cout << ",\"checker\":";
                strings(pqc_poly::check_mlkem_plan(request, analyzed));
                std::cout << '}';
            }
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
    // writes both traversal backends for the Python order recorder
    for (const auto traversal : {pqc_poly::ntt_traversal::stage_major, pqc_poly::ntt_traversal::fuse_two_layers})
    {
        auto plan = plans.front();
        plan.forward = traversal;
        std::ofstream file(directory / (std::string(pqc_poly::ntt_traversal_name(traversal)) + ".c"));
        file << pqc_poly::generate_mlkem_backend({}, pqc_poly::analyze_mlkem_plan({}, plan));
        if (!file) return 3;
    }
}
