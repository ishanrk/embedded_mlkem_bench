#include <iostream>

namespace pqc_poly
{
int mlkem_run(int argc, char **argv, std::ostream &output, std::ostream &error);
}

// thin process entry keeps CLI behavior testable with substitute output streams
int main(int argc, char **argv)
{
    return pqc_poly::mlkem_run(argc, argv, std::cout, std::cerr);
}
