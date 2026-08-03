use crate::common::{add_shifted, add_slices, fold_mod_q, multiply_regular, pad_to_512, subtract_from, LINEAR_SIZE};
use pqc_poly_ring::{PolyArray, SignedPolyArray};

const LEAF_SIZE: usize = 32;

fn multiply_recursive(left: &[u16], right: &[u16]) -> Vec<u16>
{
    if left.len() <= LEAF_SIZE
    {
        return multiply_regular(left, right);
    }

    let split = left.len() / 2;
    let low_product = multiply_recursive(&left[..split], &right[..split]);
    let high_product = multiply_recursive(&left[split..], &right[split..]);
    let left_sum = add_slices(&left[..split], &left[split..]);
    let right_sum = add_slices(&right[..split], &right[split..]);
    let mut middle_product = multiply_recursive(&left_sum, &right_sum);

    subtract_from(&mut middle_product, &low_product);
    subtract_from(&mut middle_product, &high_product);

    let mut product = vec![0u16; left.len() + right.len() - 1];

    add_shifted(&mut product, &low_product, 0);
    add_shifted(&mut product, &middle_product, split);
    add_shifted(&mut product, &high_product, split * 2);
    product
}

pub fn karatsuba(left: &SignedPolyArray, right: &SignedPolyArray) -> PolyArray
{
    let left_padded = pad_to_512(left);
    let right_padded = pad_to_512(right);
    let product = multiply_recursive(&left_padded, &right_padded);

    fold_mod_q(&product[..LINEAR_SIZE])
}

#[cfg(test)]
mod tests
{
    use super::*;
    use pqc_poly_ring::reference_multiply;

    #[test]
    fn karatsuba_matches_reference()
    {
        let (left, right) = crate::test_pair(1);

        assert_eq!(karatsuba(&left, &right), reference_multiply(&left, &right));
    }
}
