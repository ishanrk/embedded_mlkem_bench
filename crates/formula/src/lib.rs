mod common;
mod karatsuba;
mod ntt;
mod tmvp;
mod toom_cook;

pub use karatsuba::karatsuba;
pub use ntt::ntt;
pub use tmvp::tmvp;
pub use toom_cook::toom_cook;

#[cfg(test)]
fn test_pair(seed: usize) -> (pqc_poly_ring::SignedPolyArray, pqc_poly_ring::SignedPolyArray)
{
    use pqc_poly_ring::N;

    let mut left = [0i16; N];
    let mut right = [0i16; N];

    for index in 0..N
    {
        left[index] = ((index * 37 + seed * 11) % 2048) as i16 - 1024;
        right[index] = ((index * 73 + seed * 19) % 2048) as i16 - 1024;
    }

    (left, right)
}

#[cfg(test)]
mod tests
{
    use super::*;
    use pqc_poly_ring::{reference_multiply, SignedPolyArray, N};

    fn assert_methods_match(left: &SignedPolyArray, right: &SignedPolyArray)
    {
        let expected = reference_multiply(left, right);

        assert_eq!(karatsuba(left, right), expected);
        assert_eq!(ntt(left, right), expected);
        assert_eq!(toom_cook(left, right), expected);
        assert_eq!(tmvp(left, right), expected);
    }

    #[test]
    fn methods_match_dense_polynomials()
    {
        let (left, right) = test_pair(5);

        assert_methods_match(&left, &right);
    }

    #[test]
    fn methods_match_wrapped_polynomials()
    {
        let mut left = [0i16; N];
        let mut right = [0i16; N];

        left[0] = -713;
        left[N - 1] = 1023;
        right[0] = 997;
        right[1] = -881;
        right[N - 1] = 619;

        assert_methods_match(&left, &right);
    }
}
