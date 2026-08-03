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
