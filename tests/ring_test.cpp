#include "pqc_poly/ring.hpp"

#include <algorithm>
int main()
{
    pqc_poly::signed_poly a{};
    pqc_poly::signed_poly b{};
    pqc_poly::poly r{};

    a[pqc_poly::poly_n - 1] = 1;
    b[1] = 1;
    pqc_poly::reference_multiply(r, a, b);

    if (r[0] != 1 || !std::all_of(r.begin() + 1, r.end(), [](std::uint16_t v) { return v == 0; }))
    {
        return 1;
    }

    a.fill(0);
    b.fill(0);
    a[0] = -1;
    b[0] = 1;
    pqc_poly::reference_multiply(r, a, b);

    return r[0] == pqc_poly::poly_q - 1 ? 0 : 1;
}
