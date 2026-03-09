use crate::common::{fold_mod_q, normalize, LINEAR_SIZE};
use pqc_poly_ring::{PolyArray, SignedPolyArray, N, Q};

const NTT_SIZE: usize = 1024;
const PRIME0: u64 = 40961;
const PRIME1: u64 = 65537;
const CRT_INVERSE: u64 = 43694;
const GENERATOR: u64 = 3;

fn power_mod(mut base: u64, mut exponent: u64, modulus: u64) -> u64
{
    let mut result = 1u64;

    while exponent != 0
    {
        if exponent & 1 != 0
        {
            result = result * base % modulus;
        }

        base = base * base % modulus;
        exponent >>= 1;
    }

    result
}

fn inverse_mod(value: u64, modulus: u64) -> u64
{
    power_mod(value, modulus - 2, modulus)
}

fn transform(values: &mut [u64], modulus: u64, inverse: bool)
{
    let mut reverse = 0usize;

    for index in 1..values.len()
    {
        let mut bit = values.len() >> 1;

        while reverse & bit != 0
        {
            reverse ^= bit;
            bit >>= 1;
        }

        reverse ^= bit;

        if index < reverse
        {
            values.swap(index, reverse);
        }
    }

    let mut length = 2usize;

    while length <= values.len()
    {
        let mut root = power_mod(GENERATOR, (modulus - 1) / length as u64, modulus);

        if inverse
        {
            root = inverse_mod(root, modulus);
        }

        for start in (0..values.len()).step_by(length)
        {
            let mut factor = 1u64;

            for offset in 0..length / 2
            {
                let low = values[start + offset];
                let high = values[start + offset + length / 2] * factor % modulus;
                let sum = low + high;

                values[start + offset] = if sum >= modulus
                {
                    sum - modulus
                }
                else
                {
                    sum
                };
                values[start + offset + length / 2] = if low >= high
                {
                    low - high
                }
                else
                {
                    low + modulus - high
                };
                factor = factor * root % modulus;
            }
        }

        length *= 2;
    }

    if inverse
    {
        let scale = inverse_mod(values.len() as u64, modulus);

        for value in values
        {
            *value = *value * scale % modulus;
        }
    }
}

fn convolve(left: &SignedPolyArray, right: &SignedPolyArray, modulus: u64) -> Vec<u64>
{
    let mut left_values = vec![0u64; NTT_SIZE];
    let mut right_values = vec![0u64; NTT_SIZE];

    for index in 0..N
    {
        left_values[index] = u64::from(normalize(left[index]));
        right_values[index] = u64::from(normalize(right[index]));
    }

    transform(&mut left_values, modulus, false);
    transform(&mut right_values, modulus, false);

    for index in 0..NTT_SIZE
    {
        left_values[index] = left_values[index] * right_values[index] % modulus;
    }

    transform(&mut left_values, modulus, true);
    left_values.truncate(LINEAR_SIZE);
    left_values
}

fn combine_residues(first: u64, second: u64) -> u16
{
    let difference = (second + PRIME1 - first % PRIME1) % PRIME1;
    let value = first + PRIME0 * (difference * CRT_INVERSE % PRIME1);

    (value & u64::from(Q - 1)) as u16
}

pub fn ntt(left: &SignedPolyArray, right: &SignedPolyArray) -> PolyArray
{
    // two primes keep crt reconstruction exact before reduction by q
    let first_product = convolve(left, right, PRIME0);
    let second_product = convolve(left, right, PRIME1);
    let mut linear = vec![0u16; LINEAR_SIZE];

    for index in 0..LINEAR_SIZE
    {
        linear[index] = combine_residues(first_product[index], second_product[index]);
    }

    fold_mod_q(&linear)
}

#[cfg(test)]
mod tests
{
    use super::*;
    use pqc_poly_ring::reference_multiply;

    #[test]
    fn ntt_matches_reference()
    {
        let (left, right) = crate::test_pair(2);

        assert_eq!(ntt(&left, &right), reference_multiply(&left, &right));
    }
}
