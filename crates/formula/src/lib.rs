use pqc_poly_ring::{mul as reference_multiply, PolyArray, SignedPolyArray, N, Q};
use std::time::Instant;

#[derive(Debug, serde::Serialize)]
pub struct MultiplicationReport
{
    // these say exactly which multiplication method produced the report
    pub name: String,
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
    pub hardware_area: Option<u64>,

    // every method gets checked against the slow and obvious reference
    pub verified_against_reference: bool,
}

#[derive(Default)]
struct Counter
{
    additions: u64,
    multiplications: u64,
    loads: u64,
    stores: u64,
    live_temporary_bytes: usize,
    peak_temporary_bytes: usize,
}

impl Counter
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

fn add_slices(left: &[u16], right: &[u16], counter: &mut Counter) -> Vec<u16>
{
    let mut sum = Vec::with_capacity(left.len());

    counter.record_buffer_allocation(left.len());

    for (index, left_value) in left.iter().enumerate()
    {
        // the 255/254 split is uneven, so a missing high coefficient acts like zero
        let right_value = right.get(index).copied().unwrap_or(0);

        sum.push(reduce_mod_q(i32::from(*left_value) + i32::from(right_value)));
        counter.additions += 1;
        counter.loads += 1 + u64::from(index < right.len());
    }

    sum
}

fn subtract_slices(destination: &mut [u16], source: &[u16], counter: &mut Counter)
{
    // zip stops at the shorter slice, which is exactly what the uneven split needs
    for (destination_value, source_value) in destination.iter_mut().zip(source)
    {
        *destination_value = reduce_mod_q(i32::from(*destination_value) - i32::from(*source_value));
        counter.additions += 1;
        counter.loads += 2;
        counter.stores += 1;
    }
}

fn multiply_linear(left: &[u16], right: &[u16], counter: &mut Counter) -> Vec<u16>
{
    let coefficient_count = left.len();
    let mut product = vec![0u16; coefficient_count * 2 - 1];

    // this is a linear product, so there is no degree wraparound in this function
    counter.record_buffer_allocation(product.len());

    for (left_index, left_value) in left.iter().enumerate()
    {
        // the left value stays loaded while the inner loop walks across the right side
        counter.loads += 1;

        for (right_index, right_value) in right.iter().enumerate()
        {
            let product_index = left_index + right_index;
            let total = u32::from(product[product_index])
                + u32::from(*left_value) * u32::from(*right_value);

            product[product_index] = (total & u32::from(Q - 1)) as u16;
            counter.additions += 1;
            counter.multiplications += 1;
            counter.loads += 2;
            counter.stores += 1;
        }
    }

    product
}

fn karatsuba_recursive(left: &[u16], right: &[u16], leaf_size: usize, counter: &mut Counter) -> Vec<u16>
{
    // once a piece is small enough, regular multiplication finishes that leaf
    if left.len() <= leaf_size
    {
        return multiply_linear(left, right, counter);
    }

    let split = left.len() / 2;

    // these are the low*low and high*high parts of the karatsuba formula
    let low_product = karatsuba_recursive(
        &left[..split],
        &right[..split],
        leaf_size,
        counter,
    );
    let high_product = karatsuba_recursive(
        &left[split..],
        &right[split..],
        leaf_size,
        counter,
    );

    // adding the halves lets karatsuba replace a fourth multiply with additions
    let left_sum = add_slices(
        &left[..split],
        &left[split..],
        counter,
    );
    let right_sum = add_slices(
        &right[..split],
        &right[split..],
        counter,
    );
    let mut middle_product = karatsuba_recursive(&left_sum, &right_sum, leaf_size, counter);

    // the sum buffers are done as soon as their product has been computed
    counter.record_buffer_release(left_sum.len());
    counter.record_buffer_release(right_sum.len());
    drop(left_sum);
    drop(right_sum);

    // this leaves low_left*high_right + high_left*low_right in the middle
    subtract_slices(&mut middle_product, &low_product, counter);
    subtract_slices(&mut middle_product, &high_product, counter);

    let mut product = vec![0u16; left.len() * 2 - 1];

    counter.record_buffer_allocation(product.len());

    // rebuild low + x^split*middle + x^(2*split)*high
    add_shifted(&mut product, &low_product, 0, counter);
    add_shifted(&mut product, &middle_product, split, counter);
    add_shifted(&mut product, &high_product, split * 2, counter);

    // the three child products have now been copied into the combined result
    counter.record_buffer_release(low_product.len());
    counter.record_buffer_release(middle_product.len());
    counter.record_buffer_release(high_product.len());
    drop(low_product);
    drop(middle_product);
    drop(high_product);

    product
}

fn add_shifted(destination: &mut [u16], source: &[u16], offset: usize, counter: &mut Counter)
{
    for (destination_value, source_value) in destination[offset..].iter_mut().zip(source)
    {
        *destination_value = reduce_mod_q(i32::from(*destination_value) + i32::from(*source_value));
        counter.additions += 1;
        counter.loads += 2;
        counter.stores += 1;
    }
}

fn normalize(polynomial: &SignedPolyArray, counter: &mut Counter) -> Vec<u16>
{
    let mut normalized = Vec::with_capacity(N);

    counter.record_buffer_allocation(N);

    for coefficient in polynomial
    {
        normalized.push(reduce_mod_q(i32::from(*coefficient)));
        counter.loads += 1;
    }

    normalized
}

fn pad_to_512(polynomial: &SignedPolyArray, counter: &mut Counter) -> Vec<u16>
{
    // 512 splits evenly all the way down to both requested leaf sizes
    let mut padded = vec![0u16; 512];

    counter.record_buffer_allocation(padded.len());

    for (index, coefficient) in polynomial.iter().enumerate()
    {
        padded[index] = reduce_mod_q(i32::from(*coefficient));
        counter.loads += 1;
        counter.stores += 1;
    }

    padded
}

fn fold_into_ring(linear: &[u16], counter: &mut Counter) -> PolyArray
{
    let mut ring = [0u16; N];

    // the returned output is not temporary memory, but initializing it is still a store
    counter.stores += N as u64;

    for (index, coefficient) in linear.iter().enumerate()
    {
        // x^509 equals one, so every high degree wraps back into the 509 slots
        let ring_index = index % N;

        ring[ring_index] = reduce_mod_q(
            i32::from(ring[ring_index]) + i32::from(*coefficient),
        );
        counter.additions += 1;
        counter.loads += 2;
        counter.stores += 1;
    }

    ring
}

fn make_report(name: &str, algorithm: &str, schedule: &str, counter: Counter, host_nanoseconds: u64, verified_against_reference: bool) -> MultiplicationReport
{
    MultiplicationReport
    {
        name: name.into(),
        algorithm: algorithm.into(),
        schedule: schedule.into(),
        temporary_bytes: counter.peak_temporary_bytes,
        additions: counter.additions,
        multiplications: counter.multiplications,
        loads: counter.loads,
        stores: counter.stores,
        host_nanoseconds: Some(host_nanoseconds),
        riscv_cycles: None,
        custom_instructions: Vec::new(),
        hardware_area: None,
        verified_against_reference,
    }
}

fn multiply_regular(left: &SignedPolyArray, right: &SignedPolyArray, counter: &mut Counter) -> PolyArray
{
    let mut product = [0u16; N];

    counter.stores += N as u64;

    for (left_index, left_value) in left.iter().enumerate()
    {
        // this value stays loaded while it is multiplied by the entire other polynomial
        let left_reduced = reduce_mod_q(i32::from(*left_value));

        counter.loads += 1;

        for (right_index, right_value) in right.iter().enumerate()
        {
            let linear_index = left_index + right_index;
            let product_index = if linear_index >= N
            {
                linear_index - N
            }
            else
            {
                linear_index
            };
            let right_reduced = reduce_mod_q(i32::from(*right_value));
            let total = u32::from(product[product_index])
                + u32::from(left_reduced) * u32::from(right_reduced);

            product[product_index] = (total & u32::from(Q - 1)) as u16;
            counter.additions += 1;
            counter.multiplications += 1;
            counter.loads += 2;
            counter.stores += 1;
        }
    }

    product
}

fn run_karatsuba(left: &SignedPolyArray, right: &SignedPolyArray, leaf_size: usize, name: &str, schedule: &str) -> (PolyArray, MultiplicationReport)
{
    let mut counter = Counter::default();
    let start_time = Instant::now();
    let left_padded = pad_to_512(left, &mut counter);
    let right_padded = pad_to_512(right, &mut counter);
    let linear_product = karatsuba_recursive(
        &left_padded,
        &right_padded,
        leaf_size,
        &mut counter,
    );

    // the last six slots came only from padding and are guaranteed to be zero
    let product = fold_into_ring(&linear_product[..N * 2 - 1], &mut counter);
    let host_nanoseconds = start_time.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    counter.record_buffer_release(left_padded.len());
    counter.record_buffer_release(right_padded.len());
    counter.record_buffer_release(linear_product.len());
    debug_assert_eq!(counter.live_temporary_bytes, 0);

    // verification happens after timing so the reference does not inflate the result
    let verified_against_reference = product == reference_multiply(left, right);
    let report = make_report(
        name,
        "karatsuba",
        schedule,
        counter,
        host_nanoseconds,
        verified_against_reference,
    );

    (product, report)
}

fn run_split_karatsuba(left: &SignedPolyArray, right: &SignedPolyArray) -> (PolyArray, MultiplicationReport)
{
    let mut counter = Counter::default();
    let start_time = Instant::now();
    let left_normalized = normalize(left, &mut counter);
    let right_normalized = normalize(right, &mut counter);

    // 509 becomes a 255-coefficient low half and a 254-coefficient high half
    let low_product = multiply_linear(
        &left_normalized[..255],
        &right_normalized[..255],
        &mut counter,
    );
    let high_product = multiply_linear(
        &left_normalized[255..],
        &right_normalized[255..],
        &mut counter,
    );
    let left_sum = add_slices(
        &left_normalized[..255],
        &left_normalized[255..],
        &mut counter,
    );
    let right_sum = add_slices(
        &right_normalized[..255],
        &right_normalized[255..],
        &mut counter,
    );
    let mut middle_product = multiply_linear(
        &left_sum,
        &right_sum,
        &mut counter,
    );

    // the half sums are dead once the middle multiplication is finished
    counter.record_buffer_release(left_sum.len());
    counter.record_buffer_release(right_sum.len());
    drop(left_sum);
    drop(right_sum);

    // remove low*low and high*high to leave the two cross products
    subtract_slices(&mut middle_product, &low_product, &mut counter);
    subtract_slices(&mut middle_product, &high_product, &mut counter);

    let mut linear_product = vec![0u16; N * 2 - 1];

    counter.record_buffer_allocation(linear_product.len());
    add_shifted(&mut linear_product, &low_product, 0, &mut counter);
    add_shifted(&mut linear_product, &middle_product, 255, &mut counter);
    add_shifted(&mut linear_product, &high_product, 510, &mut counter);

    counter.record_buffer_release(low_product.len());
    counter.record_buffer_release(middle_product.len());
    counter.record_buffer_release(high_product.len());
    drop(low_product);
    drop(middle_product);
    drop(high_product);

    let product = fold_into_ring(&linear_product, &mut counter);
    let host_nanoseconds = start_time.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    counter.record_buffer_release(left_normalized.len());
    counter.record_buffer_release(right_normalized.len());
    counter.record_buffer_release(linear_product.len());
    debug_assert_eq!(counter.live_temporary_bytes, 0);

    let verified_against_reference = product == reference_multiply(left, right);
    let report = make_report(
        "karatsuba-single-split",
        "karatsuba",
        "split-255-regular-leaves",
        counter,
        host_nanoseconds,
        verified_against_reference,
    );

    (product, report)
}

pub fn regular_509(left: &SignedPolyArray, right: &SignedPolyArray) -> (PolyArray, MultiplicationReport)
{
    let mut counter = Counter::default();
    let start_time = Instant::now();
    let product = multiply_regular(left, right, &mut counter);
    let host_nanoseconds = start_time.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64;

    let verified_against_reference = product == reference_multiply(left, right);
    let report = make_report(
        "regular-509",
        "regular",
        "direct-cyclic",
        counter,
        host_nanoseconds,
        verified_against_reference,
    );

    (product, report)
}

pub fn karatsuba_single_split(left: &SignedPolyArray, right: &SignedPolyArray) -> (PolyArray, MultiplicationReport)
{
    run_split_karatsuba(left, right)
}

pub fn karatsuba_leaf_32(left: &SignedPolyArray, right: &SignedPolyArray) -> (PolyArray, MultiplicationReport)
{
    run_karatsuba(
        left,
        right,
        32,
        "karatsuba-leaf-32",
        "recursive-leaf-32",
    )
}

pub fn karatsuba_leaf_16(left: &SignedPolyArray, right: &SignedPolyArray) -> (PolyArray, MultiplicationReport)
{
    run_karatsuba(
        left,
        right,
        16,
        "karatsuba-leaf-16",
        "recursive-leaf-16",
    )
}

#[cfg(test)]
mod tests
{
    use super::*;

    #[test]
    fn all_methods_match_reference()
    {
        let mut left = [0i16; N];
        let mut right = [0i16; N];

        // this is dense and deterministic, so one test still exercises every region
        for index in 0..N
        {
            left[index] = ((index * 37 + 11) % 4096) as i16 - 2048;
            right[index] = ((index * 73 + 19) % 4096) as i16 - 2048;
        }

        let runs = [
            regular_509(&left, &right),
            karatsuba_single_split(&left, &right),
            karatsuba_leaf_32(&left, &right),
            karatsuba_leaf_16(&left, &right),
        ];
        let expected_multiplications = [259081, 194566, 82944, 62208];

        for ((_, report), expected_count) in runs.into_iter().zip(expected_multiplications)
        {
            assert!(report.verified_against_reference);
            assert_eq!(report.multiplications, expected_count);
            assert!(report.loads > 0);
            assert!(report.stores > 0);
            assert_eq!(report.riscv_cycles, None);
            assert_eq!(report.hardware_area, None);
        }
    }
}
