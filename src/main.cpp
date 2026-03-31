#include "pqc_poly/explore.hpp"

#include <iostream>
#include <span>
#include <string_view>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<std::string_view> arguments;

    arguments.reserve(static_cast<std::size_t>(argc > 0 ? argc - 1 : 0));
    for (int i = 1; i < argc; ++i)
    {
        arguments.emplace_back(argv[i]);
    }

    return pqc_poly::run(arguments, std::cout, std::cerr);
}
