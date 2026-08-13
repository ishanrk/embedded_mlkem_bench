use crate::Request;

pub fn required_signed_bits(bound: u128) -> u16
{
    if bound == 0
    {
        return 1;
    }

    (u128::BITS - bound.leading_zeros() + 1) as u16
}

pub fn product_bound(request: &Request) -> u128
{
    let input_bound = u128::from(request.input_bound());

    input_bound * input_bound
}

pub fn accumulator_bound(request: &Request) -> u128
{
    u128::from(request.coefficient_count()) * product_bound(request)
}

pub fn signed_width_fits(bound: u128, bits: u16) -> bool
{
    if bits == 0 || bits > 128
    {
        return false;
    }
    if bits == 128
    {
        return bound <= i128::MAX as u128;
    }

    bound < 1u128 << (bits - 1)
}

#[cfg(test)]
mod tests
{
    use super::*;
    use crate::parse_request;

    #[test]
    fn signed_width_includes_the_sign_bit()
    {
        assert_eq!(required_signed_bits(0), 1);
        assert_eq!(required_signed_bits(1), 2);
        assert_eq!(required_signed_bits(7), 4);
        assert_eq!(required_signed_bits(8), 5);
        assert!(signed_width_fits(7, 4));
        assert!(!signed_width_fits(8, 4));
        assert!(!signed_width_fits(0, 0));
    }

    #[test]
    fn canonical_and_centered_bounds_match_the_reference()
    {
        let canonical = parse_request(
            r#"{"op":"negacyclic_mul","n":8,"q":17,"input":"canonical"}"#,
        )
        .unwrap();
        let centered = parse_request(
            r#"{"op":"cyclic_mul","n":8,"q":17,"input":"centered"}"#,
        )
        .unwrap();

        assert_eq!(product_bound(&canonical), 16 * 16);
        assert_eq!(accumulator_bound(&canonical), 8 * 16 * 16);
        assert_eq!(required_signed_bits(accumulator_bound(&canonical)), 13);
        assert_eq!(accumulator_bound(&centered), 8 * 8 * 8);
    }
}
