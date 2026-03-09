use crate::common::{add_slices, normalize, reduce_mod_q, subtract_slices, PADDED_SIZE};
use pqc_poly_ring::{PolyArray, SignedPolyArray, N};

const LEAF_SIZE: usize = 16;

fn multiply_direct(diagonals: &[u16], vector: &[u16]) -> Vec<u16>
{
    let size = vector.len();
    let mut product = vec![0u16; size];

    for (row, output) in product.iter_mut().enumerate()
    {
        let mut sum = 0u32;

        for (column, value) in vector.iter().enumerate()
        {
            let diagonal = diagonals[size - 1 + row - column];

            sum += u32::from(diagonal) * u32::from(*value);
            sum &= 0x7ff;
        }

        *output = sum as u16;
    }

    product
}

fn multiply_recursive(diagonals: &[u16], vector: &[u16]) -> Vec<u16>
{
    let size = vector.len();

    if size <= LEAF_SIZE
    {
        return multiply_direct(diagonals, vector);
    }

    let half = size / 2;
    let first_vector = &vector[..half];
    let second_vector = &vector[half..];
    let middle_diagonals = &diagonals[half..half * 3 - 1];
    let vector_sum = add_slices(first_vector, second_vector);
    let upper_difference = subtract_slices(&diagonals[..half * 2 - 1], middle_diagonals);
    let lower_difference = subtract_slices(middle_diagonals, &diagonals[half * 2..]);

    // split one tmvp into three half-size products
    let shared_product = multiply_recursive(middle_diagonals, &vector_sum);
    let upper_product = multiply_recursive(&upper_difference, second_vector);
    let lower_product = multiply_recursive(&lower_difference, first_vector);
    let mut product = vec![0u16; size];

    for index in 0..half
    {
        product[index] = reduce_mod_q(i32::from(shared_product[index]) + i32::from(upper_product[index]));
        product[index + half] = reduce_mod_q(i32::from(shared_product[index]) - i32::from(lower_product[index]));
    }

    product
}

pub fn tmvp(left: &SignedPolyArray, right: &SignedPolyArray) -> PolyArray
{
    let mut diagonals = vec![0u16; PADDED_SIZE * 2 - 1];
    let mut vector = vec![0u16; PADDED_SIZE];
    let span = (N - 1) as isize;

    for diagonal in -span..=span
    {
        let source = diagonal.rem_euclid(N as isize) as usize;
        let index = (PADDED_SIZE as isize - 1 + diagonal) as usize;

        diagonals[index] = normalize(left[source]);
    }

    for index in 0..N
    {
        vector[index] = normalize(right[index]);
    }

    let result = multiply_recursive(&diagonals, &vector);
    let mut product = [0u16; N];

    product.copy_from_slice(&result[..N]);
    product
}

#[cfg(test)]
mod tests
{
    use super::*;
    use pqc_poly_ring::reference_multiply;

    #[test]
    fn tmvp_matches_reference()
    {
        let (left, right) = crate::test_pair(4);

        assert_eq!(tmvp(&left, &right), reference_multiply(&left, &right));
    }
}
