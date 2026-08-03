use crate::common::fold_exact;
use pqc_poly_ring::{PolyArray, SignedPolyArray, N};

const BLOCK_SIZE: usize = 170;
const PADDED_SIZE: usize = BLOCK_SIZE * 3;
const BLOCK_PRODUCT_SIZE: usize = BLOCK_SIZE * 2 - 1;

fn split_polynomial(poly: &SignedPolyArray) -> [Vec<i64>; 3]
{
    let mut parts = [vec![0i64; BLOCK_SIZE], vec![0i64; BLOCK_SIZE], vec![0i64; BLOCK_SIZE]];

    for index in 0..N
    {
        parts[index / BLOCK_SIZE][index % BLOCK_SIZE] = i64::from(poly[index]);
    }

    parts
}

fn evaluate(parts: &[Vec<i64>; 3], point: i64) -> Vec<i64>
{
    let point_squared = point * point;

    (0..BLOCK_SIZE)
        .map(|index| parts[0][index] + point * parts[1][index] + point_squared * parts[2][index])
        .collect()
}

fn multiply_regular(left: &[i64], right: &[i64]) -> Vec<i64>
{
    let mut product = vec![0i64; left.len() + right.len() - 1];

    for (left_index, left_value) in left.iter().enumerate()
    {
        for (right_index, right_value) in right.iter().enumerate()
        {
            product[left_index + right_index] += left_value * right_value;
        }
    }

    product
}

fn add_shifted(product: &mut [i64], part: &[i64], offset: usize)
{
    for (output, value) in product[offset..].iter_mut().zip(part)
    {
        *output += value;
    }
}

pub fn toom_cook(left: &SignedPolyArray, right: &SignedPolyArray) -> PolyArray
{
    let left_parts = split_polynomial(left);
    let right_parts = split_polynomial(right);
    let at_zero = multiply_regular(&left_parts[0], &right_parts[0]);
    let at_one = multiply_regular(&evaluate(&left_parts, 1), &evaluate(&right_parts, 1));
    let at_minus_one = multiply_regular(&evaluate(&left_parts, -1), &evaluate(&right_parts, -1));
    let at_two = multiply_regular(&evaluate(&left_parts, 2), &evaluate(&right_parts, 2));
    let at_infinity = multiply_regular(&left_parts[2], &right_parts[2]);
    let mut blocks = [
        vec![0i64; BLOCK_PRODUCT_SIZE],
        vec![0i64; BLOCK_PRODUCT_SIZE],
        vec![0i64; BLOCK_PRODUCT_SIZE],
        vec![0i64; BLOCK_PRODUCT_SIZE],
        vec![0i64; BLOCK_PRODUCT_SIZE],
    ];

    for index in 0..BLOCK_PRODUCT_SIZE
    {
        let even_sum = at_one[index] + at_minus_one[index];
        let odd_difference = at_one[index] - at_minus_one[index];

        debug_assert_eq!(even_sum % 2, 0);
        debug_assert_eq!(odd_difference % 2, 0);

        blocks[0][index] = at_zero[index];
        blocks[4][index] = at_infinity[index];
        blocks[2][index] = even_sum / 2 - blocks[0][index] - blocks[4][index];

        let odd_sum = odd_difference / 2;
        let scaled_odd = at_two[index] - blocks[0][index] - 4 * blocks[2][index] - 16 * blocks[4][index];

        debug_assert_eq!(scaled_odd % 2, 0);

        let weighted_odd = scaled_odd / 2;

        debug_assert_eq!((weighted_odd - odd_sum) % 3, 0);

        blocks[3][index] = (weighted_odd - odd_sum) / 3;
        blocks[1][index] = odd_sum - blocks[3][index];
    }

    let mut linear = vec![0i64; PADDED_SIZE * 2 - 1];

    for (block_index, block) in blocks.iter().enumerate()
    {
        add_shifted(&mut linear, block, block_index * BLOCK_SIZE);
    }

    fold_exact(&linear)
}

#[cfg(test)]
mod tests
{
    use super::*;
    use pqc_poly_ring::reference_multiply;

    #[test]
    fn toom_cook_matches_reference()
    {
        let (left, right) = crate::test_pair(3);

        assert_eq!(toom_cook(&left, &right), reference_multiply(&left, &right));
    }
}
