use pqc_poly_ring::{PolyArray, SignedPolyArray, N, Q};

pub(crate) const PADDED_SIZE: usize = 512;
pub(crate) const LINEAR_SIZE: usize = N * 2 - 1;

pub(crate) fn reduce_mod_q(value: i32) -> u16
{
    (value as u16) & (Q - 1)
}

pub(crate) fn normalize(value: i16) -> u16
{
    reduce_mod_q(i32::from(value))
}

pub(crate) fn pad_to_512(poly: &SignedPolyArray) -> Vec<u16>
{
    let mut padded = vec![0u16; PADDED_SIZE];

    for (index, value) in poly.iter().enumerate()
    {
        padded[index] = normalize(*value);
    }

    padded
}

pub(crate) fn add_slices(left: &[u16], right: &[u16]) -> Vec<u16>
{
    left.iter()
        .zip(right)
        .map(|(left_value, right_value)| reduce_mod_q(i32::from(*left_value) + i32::from(*right_value)))
        .collect()
}

pub(crate) fn subtract_slices(left: &[u16], right: &[u16]) -> Vec<u16>
{
    left.iter()
        .zip(right)
        .map(|(left_value, right_value)| reduce_mod_q(i32::from(*left_value) - i32::from(*right_value)))
        .collect()
}

pub(crate) fn subtract_from(left: &mut [u16], right: &[u16])
{
    for (left_value, right_value) in left.iter_mut().zip(right)
    {
        *left_value = reduce_mod_q(i32::from(*left_value) - i32::from(*right_value));
    }
}

pub(crate) fn multiply_regular(left: &[u16], right: &[u16]) -> Vec<u16>
{
    let mut product = vec![0u16; left.len() + right.len() - 1];

    for (left_index, left_value) in left.iter().enumerate()
    {
        for (right_index, right_value) in right.iter().enumerate()
        {
            let index = left_index + right_index;
            let value = u32::from(product[index]) + u32::from(*left_value) * u32::from(*right_value);

            product[index] = (value & u32::from(Q - 1)) as u16;
        }
    }

    product
}

pub(crate) fn add_shifted(product: &mut [u16], part: &[u16], offset: usize)
{
    for (output, value) in product[offset..].iter_mut().zip(part)
    {
        *output = reduce_mod_q(i32::from(*output) + i32::from(*value));
    }
}

pub(crate) fn fold_mod_q(linear: &[u16]) -> PolyArray
{
    let mut product = [0u16; N];

    for (index, value) in linear.iter().enumerate()
    {
        let output_index = index % N;

        product[output_index] = reduce_mod_q(i32::from(product[output_index]) + i32::from(*value));
    }

    product
}

pub(crate) fn fold_exact(linear: &[i64]) -> PolyArray
{
    let mut folded = [0i64; N];

    for (index, value) in linear.iter().enumerate()
    {
        folded[index % N] += value;
    }

    let mut product = [0u16; N];

    for (index, value) in folded.iter().enumerate()
    {
        product[index] = value.rem_euclid(i64::from(Q)) as u16;
    }

    product
}
