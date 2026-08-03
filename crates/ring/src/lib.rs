#![no_std]

pub const N: usize = 509;
pub const Q: u16 = 2048;

pub type SignedPolyArray = [i16; N];
pub type PolyArray = [u16; N];

pub fn reference_multiply(left: &SignedPolyArray, right: &SignedPolyArray) -> PolyArray
{
    let mut product = [0i64; N];

    for (left_index, left_value) in left.iter().enumerate()
    {
        for (right_index, right_value) in right.iter().enumerate()
        {
            let index = (left_index + right_index) % N;

            product[index] += i64::from(*left_value) * i64::from(*right_value);
        }
    }

    let mut reduced = [0u16; N];

    for (index, value) in product.iter().enumerate()
    {
        reduced[index] = value.rem_euclid(i64::from(Q)) as u16;
    }

    reduced
}
