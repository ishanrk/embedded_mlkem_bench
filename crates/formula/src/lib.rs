use pqc_poly_ring::{mul as reference_multiply, PolyArray, SignedPolyArray, N, Q};
use std::time::Instant;

#[derive(Debug, serde::Serialize)]
pub struct DesignResult
{
    // these three fields make the design easy to identify in result files
    pub id: String,
    pub algorithm: String,
    pub schedule: String,

    // these are logical software costs, not hardware performance counters
    pub temporary_bytes: usize,
    pub additions: u64,
    pub multiplications: u64,
    pub loads: u64,
    pub stores: u64,

    // only host timing exists right now, the hardware fields come later
    pub host_nanoseconds: Option<u64>,
    pub riscv_cycles: Option<u64>,
    pub custom_instructions: Vec<String>,
    pub extension_area: Option<u64>,

    // every design gets checked against the slow and obvious reference
    pub verified_against_reference: bool,
}

#[derive(Default)]
struct OperationCounts
{
    additions: u64,
    multiplications: u64,
    loads: u64,
    stores: u64,
    live_temporary_bytes: usize,
    peak_temporary_bytes: usize,
}

impl OperationCounts
{
    fn record_buffer_allocation(&mut self, coefficient_count: usize)
    {
        // every temporary coefficient is stored as a two-byte u16
        self.live_temporary_bytes += coefficient_count * 2;
        self.peak_temporary_bytes = self.peak_temporary_bytes.max(self.live_temporary_bytes);

        // creating a coefficient buffer means each slot gets written once
        self.stores += coefficient_count as u64;
    }

    fn record_buffer_release(&mut self, coefficient_count: usize)
    {
        // the buffer is dead here, so its bytes stop counting toward the live total
        self.live_temporary_bytes -= coefficient_count * 2;
    }
}

fn reduce_mod_q(value: i32) -> u16
{
    // q is 2048, so keeping the low eleven bits is the same as modulo q
    (value as u16) & (Q - 1)
}

fn add_polynomial_slices(left_coefficients: &[u16], right_coefficients: &[u16], counts: &mut OperationCounts) -> Vec<u16>
{
    let mut sum_coefficients = Vec::with_capacity(left_coefficients.len());

    counts.record_buffer_allocation(left_coefficients.len());

    for (coefficient_index, left_coefficient) in left_coefficients.iter().enumerate()
    {
        // the 255/254 split is uneven, so a missing high coefficient acts like zero
        let right_coefficient = right_coefficients.get(coefficient_index).copied().unwrap_or(0);

        sum_coefficients.push(reduce_mod_q(i32::from(*left_coefficient) + i32::from(right_coefficient)));
        counts.additions += 1;
        counts.loads += 1 + u64::from(coefficient_index < right_coefficients.len());
    }

    sum_coefficients
}

fn subtract_polynomial_slices_in_place(destination_coefficients: &mut [u16], source_coefficients: &[u16], counts: &mut OperationCounts)
{
    // zip stops at the shorter slice, which is exactly what the uneven split needs
    for (destination_coefficient, source_coefficient) in destination_coefficients.iter_mut().zip(source_coefficients)
    {
        *destination_coefficient = reduce_mod_q(i32::from(*destination_coefficient) - i32::from(*source_coefficient));
        counts.additions += 1;
        counts.loads += 2;
        counts.stores += 1;
    }
}

fn regular_linear_multiply(left_coefficients: &[u16], right_coefficients: &[u16], counts: &mut OperationCounts) -> Vec<u16>
{
    let coefficient_count = left_coefficients.len();
    let mut product_coefficients = vec![0u16; coefficient_count * 2 - 1];

    // this is a linear product, so there is no degree wraparound in this function
    counts.record_buffer_allocation(product_coefficients.len());

    for (left_index, left_coefficient) in left_coefficients.iter().enumerate()
    {
        // the left value stays loaded while the inner loop walks across the right side
        counts.loads += 1;

        for (right_index, right_coefficient) in right_coefficients.iter().enumerate()
        {
            let product_index = left_index + right_index;
            let accumulated_value = u32::from(product_coefficients[product_index])
                + u32::from(*left_coefficient) * u32::from(*right_coefficient);

            product_coefficients[product_index] = (accumulated_value & u32::from(Q - 1)) as u16;
            counts.additions += 1;
            counts.multiplications += 1;
            counts.loads += 2;
            counts.stores += 1;
        }
    }

    product_coefficients
}

fn recursive_karatsuba_multiply(left_coefficients: &[u16], right_coefficients: &[u16], leaf_size: usize, counts: &mut OperationCounts) -> Vec<u16>
{
    // once a piece is small enough, regular multiplication finishes that leaf
    if left_coefficients.len() <= leaf_size
    {
        return regular_linear_multiply(left_coefficients, right_coefficients, counts);
    }

    let split_index = left_coefficients.len() / 2;

    // these are the low*low and high*high parts of the karatsuba formula
    let low_product = recursive_karatsuba_multiply(
        &left_coefficients[..split_index],
        &right_coefficients[..split_index],
        leaf_size,
        counts,
    );
    let high_product = recursive_karatsuba_multiply(
        &left_coefficients[split_index..],
        &right_coefficients[split_index..],
        leaf_size,
        counts,
    );

    // adding the halves lets karatsuba replace a fourth multiply with additions
    let left_half_sum = add_polynomial_slices(
        &left_coefficients[..split_index],
        &left_coefficients[split_index..],
        counts,
    );
    let right_half_sum = add_polynomial_slices(
        &right_coefficients[..split_index],
        &right_coefficients[split_index..],
        counts,
    );
    let mut middle_product = recursive_karatsuba_multiply(&left_half_sum, &right_half_sum, leaf_size, counts);

    // the sum buffers are done as soon as their product has been computed
    counts.record_buffer_release(left_half_sum.len());
    counts.record_buffer_release(right_half_sum.len());
    drop(left_half_sum);
    drop(right_half_sum);

    // this leaves low_left*high_right + high_left*low_right in the middle
    subtract_polynomial_slices_in_place(&mut middle_product, &low_product, counts);
    subtract_polynomial_slices_in_place(&mut middle_product, &high_product, counts);

    let mut product_coefficients = vec![0u16; left_coefficients.len() * 2 - 1];

    counts.record_buffer_allocation(product_coefficients.len());

    // rebuild low + x^split*middle + x^(2*split)*high
    add_shifted_polynomial(&mut product_coefficients, &low_product, 0, counts);
    add_shifted_polynomial(&mut product_coefficients, &middle_product, split_index, counts);
    add_shifted_polynomial(&mut product_coefficients, &high_product, split_index * 2, counts);

    // the three child products have now been copied into the combined result
    counts.record_buffer_release(low_product.len());
    counts.record_buffer_release(middle_product.len());
    counts.record_buffer_release(high_product.len());
    drop(low_product);
    drop(middle_product);
    drop(high_product);

    product_coefficients
}

fn add_shifted_polynomial(destination_coefficients: &mut [u16], source_coefficients: &[u16], coefficient_offset: usize, counts: &mut OperationCounts)
{
    for (destination_coefficient, source_coefficient) in destination_coefficients[coefficient_offset..].iter_mut().zip(source_coefficients)
    {
        *destination_coefficient = reduce_mod_q(i32::from(*destination_coefficient) + i32::from(*source_coefficient));
        counts.additions += 1;
        counts.loads += 2;
        counts.stores += 1;
    }
}

fn normalize_signed_polynomial(polynomial: &SignedPolyArray, counts: &mut OperationCounts) -> Vec<u16>
{
    let mut normalized_coefficients = Vec::with_capacity(N);

    counts.record_buffer_allocation(N);

    for coefficient in polynomial
    {
        normalized_coefficients.push(reduce_mod_q(i32::from(*coefficient)));
        counts.loads += 1;
    }

    normalized_coefficients
}

fn pad_polynomial_to_512(polynomial: &SignedPolyArray, counts: &mut OperationCounts) -> Vec<u16>
{
    // 512 splits evenly all the way down to both requested leaf sizes
    let mut padded_coefficients = vec![0u16; 512];

    counts.record_buffer_allocation(padded_coefficients.len());

    for (coefficient_index, coefficient) in polynomial.iter().enumerate()
    {
        padded_coefficients[coefficient_index] = reduce_mod_q(i32::from(*coefficient));
        counts.loads += 1;
        counts.stores += 1;
    }

    padded_coefficients
}

fn fold_linear_product_into_ring(linear_coefficients: &[u16], counts: &mut OperationCounts) -> PolyArray
{
    let mut ring_coefficients = [0u16; N];

    // the returned output is not temporary memory, but initializing it is still a store
    counts.stores += N as u64;

    for (linear_index, coefficient) in linear_coefficients.iter().enumerate()
    {
        // x^509 equals one, so every high degree wraps back into the 509 slots
        let ring_index = linear_index % N;

        ring_coefficients[ring_index] = reduce_mod_q(
            i32::from(ring_coefficients[ring_index]) + i32::from(*coefficient),
        );
        counts.additions += 1;
        counts.loads += 2;
        counts.stores += 1;
    }

    ring_coefficients
}

fn create_design_result(design_id: &str, algorithm_name: &str, schedule_name: &str, counts: OperationCounts, host_nanoseconds: u64, verified_against_reference: bool) -> DesignResult
{
    DesignResult
    {
        id: design_id.into(),
        algorithm: algorithm_name.into(),
        schedule: schedule_name.into(),
        temporary_bytes: counts.peak_temporary_bytes,
        additions: counts.additions,
        multiplications: counts.multiplications,
        loads: counts.loads,
        stores: counts.stores,
        host_nanoseconds: Some(host_nanoseconds),
        riscv_cycles: None,
        custom_instructions: Vec::new(),
        extension_area: None,
        verified_against_reference,
    }
}

fn regular_cyclic_multiply(multiplicand: &SignedPolyArray, multiplier: &SignedPolyArray, counts: &mut OperationCounts) -> PolyArray
{
    let mut product_coefficients = [0u16; N];

    counts.stores += N as u64;

    for (multiplicand_index, multiplicand_coefficient) in multiplicand.iter().enumerate()
    {
        // this value stays loaded while it is multiplied by the entire other polynomial
        let reduced_multiplicand = reduce_mod_q(i32::from(*multiplicand_coefficient));

        counts.loads += 1;

        for (multiplier_index, multiplier_coefficient) in multiplier.iter().enumerate()
        {
            let unwrapped_index = multiplicand_index + multiplier_index;
            let product_index = if unwrapped_index >= N
            {
                unwrapped_index - N
            }
            else
            {
                unwrapped_index
            };
            let reduced_multiplier = reduce_mod_q(i32::from(*multiplier_coefficient));
            let accumulated_value = u32::from(product_coefficients[product_index])
                + u32::from(reduced_multiplicand) * u32::from(reduced_multiplier);

            product_coefficients[product_index] = (accumulated_value & u32::from(Q - 1)) as u16;
            counts.additions += 1;
            counts.multiplications += 1;
            counts.loads += 2;
            counts.stores += 1;
        }
    }

    product_coefficients
}

fn run_recursive_karatsuba(multiplicand: &SignedPolyArray, multiplier: &SignedPolyArray, leaf_size: usize, design_id: &str, schedule_name: &str) -> (PolyArray, DesignResult)
{
    let mut counts = OperationCounts::default();
    let start_time = Instant::now();
    let padded_multiplicand = pad_polynomial_to_512(multiplicand, &mut counts);
    let padded_multiplier = pad_polynomial_to_512(multiplier, &mut counts);
    let linear_product = recursive_karatsuba_multiply(
        &padded_multiplicand,
        &padded_multiplier,
        leaf_size,
        &mut counts,
    );

    // the last six slots came only from padding and are guaranteed to be zero
    let product = fold_linear_product_into_ring(&linear_product[..N * 2 - 1], &mut counts);
    let host_nanoseconds = start_time.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    counts.record_buffer_release(padded_multiplicand.len());
    counts.record_buffer_release(padded_multiplier.len());
    counts.record_buffer_release(linear_product.len());
    debug_assert_eq!(counts.live_temporary_bytes, 0);

    // verification happens after timing so the reference does not inflate the result
    let verified_against_reference = product == reference_multiply(multiplicand, multiplier);
    let design_result = create_design_result(
        design_id,
        "karatsuba",
        schedule_name,
        counts,
        host_nanoseconds,
        verified_against_reference,
    );

    (product, design_result)
}

fn run_single_split_karatsuba(multiplicand: &SignedPolyArray, multiplier: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    let mut counts = OperationCounts::default();
    let start_time = Instant::now();
    let normalized_multiplicand = normalize_signed_polynomial(multiplicand, &mut counts);
    let normalized_multiplier = normalize_signed_polynomial(multiplier, &mut counts);

    // 509 becomes a 255-coefficient low half and a 254-coefficient high half
    let low_product = regular_linear_multiply(
        &normalized_multiplicand[..255],
        &normalized_multiplier[..255],
        &mut counts,
    );
    let high_product = regular_linear_multiply(
        &normalized_multiplicand[255..],
        &normalized_multiplier[255..],
        &mut counts,
    );
    let multiplicand_half_sum = add_polynomial_slices(
        &normalized_multiplicand[..255],
        &normalized_multiplicand[255..],
        &mut counts,
    );
    let multiplier_half_sum = add_polynomial_slices(
        &normalized_multiplier[..255],
        &normalized_multiplier[255..],
        &mut counts,
    );
    let mut middle_product = regular_linear_multiply(
        &multiplicand_half_sum,
        &multiplier_half_sum,
        &mut counts,
    );

    // the half sums are dead once the middle multiplication is finished
    counts.record_buffer_release(multiplicand_half_sum.len());
    counts.record_buffer_release(multiplier_half_sum.len());
    drop(multiplicand_half_sum);
    drop(multiplier_half_sum);

    // remove low*low and high*high to leave the two cross products
    subtract_polynomial_slices_in_place(&mut middle_product, &low_product, &mut counts);
    subtract_polynomial_slices_in_place(&mut middle_product, &high_product, &mut counts);

    let mut linear_product = vec![0u16; N * 2 - 1];

    counts.record_buffer_allocation(linear_product.len());
    add_shifted_polynomial(&mut linear_product, &low_product, 0, &mut counts);
    add_shifted_polynomial(&mut linear_product, &middle_product, 255, &mut counts);
    add_shifted_polynomial(&mut linear_product, &high_product, 510, &mut counts);

    counts.record_buffer_release(low_product.len());
    counts.record_buffer_release(middle_product.len());
    counts.record_buffer_release(high_product.len());
    drop(low_product);
    drop(middle_product);
    drop(high_product);

    let product = fold_linear_product_into_ring(&linear_product, &mut counts);
    let host_nanoseconds = start_time.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    counts.record_buffer_release(normalized_multiplicand.len());
    counts.record_buffer_release(normalized_multiplier.len());
    counts.record_buffer_release(linear_product.len());
    debug_assert_eq!(counts.live_temporary_bytes, 0);

    let verified_against_reference = product == reference_multiply(multiplicand, multiplier);
    let design_result = create_design_result(
        "karatsuba-single-split",
        "karatsuba",
        "split-255-regular-leaves",
        counts,
        host_nanoseconds,
        verified_against_reference,
    );

    (product, design_result)
}

pub fn regular_multiply_509(multiplicand: &SignedPolyArray, multiplier: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    let mut counts = OperationCounts::default();
    let start_time = Instant::now();
    let product = regular_cyclic_multiply(multiplicand, multiplier, &mut counts);
    let host_nanoseconds = start_time.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    let verified_against_reference = product == reference_multiply(multiplicand, multiplier);
    let design_result = create_design_result(
        "regular-509",
        "regular",
        "direct-cyclic",
        counts,
        host_nanoseconds,
        verified_against_reference,
    );

    (product, design_result)
}

pub fn karatsuba_single_split(multiplicand: &SignedPolyArray, multiplier: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    run_single_split_karatsuba(multiplicand, multiplier)
}

pub fn karatsuba_with_32_coefficient_leaves(multiplicand: &SignedPolyArray, multiplier: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    run_recursive_karatsuba(
        multiplicand,
        multiplier,
        32,
        "karatsuba-32-coefficient-leaves",
        "recursive-32-coefficient-leaves",
    )
}

pub fn karatsuba_with_16_coefficient_leaves(multiplicand: &SignedPolyArray, multiplier: &SignedPolyArray) -> (PolyArray, DesignResult)
{
    run_recursive_karatsuba(
        multiplicand,
        multiplier,
        16,
        "karatsuba-16-coefficient-leaves",
        "recursive-16-coefficient-leaves",
    )
}

#[cfg(test)]
mod tests
{
    use super::*;

    #[test]
    fn all_designs_match_the_reference()
    {
        let mut multiplicand = [0i16; N];
        let mut multiplier = [0i16; N];

        // this is dense and deterministic, so one test still exercises every region
        for coefficient_index in 0..N
        {
            multiplicand[coefficient_index] = ((coefficient_index * 37 + 11) % 4096) as i16 - 2048;
            multiplier[coefficient_index] = ((coefficient_index * 73 + 19) % 4096) as i16 - 2048;
        }

        let design_runs = [
            regular_multiply_509(&multiplicand, &multiplier),
            karatsuba_single_split(&multiplicand, &multiplier),
            karatsuba_with_32_coefficient_leaves(&multiplicand, &multiplier),
            karatsuba_with_16_coefficient_leaves(&multiplicand, &multiplier),
        ];
        let expected_multiplication_counts = [259081, 194566, 82944, 62208];

        for ((_, design_result), expected_multiplications) in design_runs.into_iter().zip(expected_multiplication_counts)
        {
            assert!(design_result.verified_against_reference);
            assert_eq!(design_result.multiplications, expected_multiplications);
            assert!(design_result.loads > 0);
            assert!(design_result.stores > 0);
            assert_eq!(design_result.riscv_cycles, None);
            assert_eq!(design_result.extension_area, None);
        }
    }
}
