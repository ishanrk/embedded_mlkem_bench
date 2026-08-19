#include <iostream>

namespace pqc_poly
{
int red32_run(int argc, char **argv, std::ostream &output, std::ostream &error);
}

int main(int argc, char **argv)
{
    return pqc_poly::red32_run(argc, argv, std::cout, std::cerr);
}
