use crate::{Aliasing, AnalysisVerdict, CandidateTrial, InputRepresentation, Request, Schedule};

fn required_signed_bits(bound: u128) -> u16
{
    if bound == 0
    {
        1
    }
    else
    {
        (u128::BITS - bound.leading_zeros() + 1) as u16
    }
}

fn signed_width_fits(bound: u128, bits: u16) -> bool
{
    match bits
    {
        1..=127 => bound < 1u128 << (bits - 1),
        128 => bound <= i128::MAX as u128,
        _ => false,
    }
}

fn target_size_is_legal(request: &Request, verdict: &AnalysisVerdict) -> bool
{
    let size_maximum = if request.target().size_bits() == 32
    {
        u128::from(u32::MAX)
    }
    else
    {
        u128::from(u64::MAX)
    };
    let object_size_maximum = if request.target().size_bits() == 32
    {
        u128::from(i32::MAX as u32)
    }
    else
    {
        i64::MAX as u128
    };
    let coefficient_count = u128::from(request.coefficient_count());
    if coefficient_count > size_maximum / 4 || coefficient_count > object_size_maximum / 4
    {
        return false;
    }

    let accumulator_bytes = u128::from(verdict.accumulator_bits() / 8);
    if accumulator_bytes == 0
    {
        return false;
    }

    match verdict.schedule()
    {
        Schedule::Full =>
        {
            let accumulator_count = 2 * coefficient_count - 1;

            accumulator_count <= size_maximum / accumulator_bytes
                && accumulator_count <= object_size_maximum / accumulator_bytes
        }
        Schedule::Fold =>
        {
            coefficient_count <= size_maximum / accumulator_bytes
                && coefficient_count <= object_size_maximum / accumulator_bytes
        }
        Schedule::Output => true,
    }
}

pub fn check_plan(request: &Request, verdict: &AnalysisVerdict) -> Vec<String>
{
    let mut errors = Vec::new();
    if verdict.schedule() == Schedule::Fold
        && !(1..=request.coefficient_count()).contains(&verdict.block_size())
    {
        errors.push("bad block".into());
    }
    if verdict.schedule() != Schedule::Fold && verdict.block_size() != 0
    {
        errors.push("unexpected block".into());
    }
    if !request
        .target()
        .accumulator_bits()
        .contains(&verdict.accumulator_bits())
        || !matches!(verdict.accumulator_bits(), 32 | 64)
    {
        errors.push("bad acc type".into());
    }

    let representative_bound = match request.input_representation()
    {
        InputRepresentation::Canonical => u128::from(request.modulus() - 1),
        InputRepresentation::Centered => u128::from(request.modulus() / 2),
    };
    let accumulator_bound = u128::from(request.coefficient_count())
        * representative_bound
        * representative_bound;
    let required_bits = required_signed_bits(accumulator_bound);
    if verdict.accumulator_bound != accumulator_bound || verdict.required_bits != required_bits
    {
        errors.push("bad range".into());
    }

    let coefficient_count = u128::from(request.coefficient_count());
    let accumulator_bytes = u128::from(verdict.accumulator_bits() / 8);
    let multiplications = coefficient_count * coefficient_count;
    let (temporary_bytes, alias_safe, additions) = match verdict.schedule()
    {
        Schedule::Full =>
        {
            (
                (2 * coefficient_count - 1) * accumulator_bytes,
                true,
                multiplications + coefficient_count - 1,
            )
        }
        Schedule::Fold =>
        {
            (coefficient_count * accumulator_bytes, true, multiplications)
        }
        Schedule::Output => (0, false, multiplications),
    };
    if verdict.temporary_bytes != temporary_bytes
    {
        errors.push("bad ram".into());
    }
    if verdict.alias_safe != alias_safe
    {
        errors.push("bad alias flag".into());
    }
    if (
        verdict.multiplications,
        verdict.additions,
        verdict.reductions,
    ) != (multiplications, additions, coefficient_count)
    {
        errors.push("bad op count".into());
    }

    let mut failure_reasons: Vec<String> = Vec::new();
    if temporary_bytes > u128::from(request.limits().ram())
    {
        failure_reasons.push("ram".into());
    }
    if !signed_width_fits(accumulator_bound, verdict.accumulator_bits())
    {
        failure_reasons.push("acc_width".into());
    }
    if request.aliasing() == Aliasing::May && !alias_safe
    {
        failure_reasons.push("alias".into());
    }
    if !target_size_is_legal(request, verdict)
    {
        failure_reasons.push("size_t".into());
    }
    if verdict.legal != failure_reasons.is_empty()
        || verdict.failure_reasons != failure_reasons
    {
        errors.push("bad legality".into());
    }

    errors
}

fn checked_cost_add(left: u128, right: u128) -> u128
{
    left.checked_add(right)
        .expect("validated request must have an exact u128 cost")
}

fn checked_cost_multiply(left: u128, right: u128) -> u128
{
    left.checked_mul(right)
        .expect("validated request must have an exact u128 cost")
}

pub fn check_trial(request: &Request, trial: &CandidateTrial) -> Vec<String>
{
    let mut errors = check_plan(request, trial.analysis());
    let coefficient_count = u128::from(request.coefficient_count());
    let multiplications = coefficient_count * coefficient_count;
    let additions = if trial.schedule() == Schedule::Full
    {
        multiplications + coefficient_count - 1
    }
    else
    {
        multiplications
    };
    let wide = trial.accumulator_bits() > request.target().word_bits();
    let mut cost = checked_cost_multiply(if wide { 10 } else { 4 }, multiplications);
    cost = checked_cost_add(cost, additions);
    cost = checked_cost_add(
        cost,
        checked_cost_multiply(if wide { 14 } else { 8 }, coefficient_count),
    );

    if trial.schedule() == Schedule::Fold
        && (1..=request.coefficient_count()).contains(&trial.block_size())
    {
        let block_size = u128::from(trial.block_size());
        let tiles = coefficient_count.div_ceil(block_size);
        cost = checked_cost_add(cost, multiplications / 4);
        cost = checked_cost_add(cost, checked_cost_multiply(2, checked_cost_multiply(tiles, tiles)));
    }
    else if trial.schedule() == Schedule::Output
    {
        cost = checked_cost_add(cost, multiplications / 2);
    }

    if trial.score.cost != cost || trial.score.model != "starter-v0"
    {
        errors.push("bad score".into());
    }

    errors
}

#[cfg(test)]
mod tests
{
    use super::*;
    use crate::{find, parse_request, pick};

    fn request() -> Request
    {
        parse_request(
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":{"name":"test","word_bits":32,"acc_bits":32},"limits":{"ram":1000}}"#,
        )
        .unwrap()
    }

    #[test]
    fn independent_checker_accepts_every_generated_trial()
    {
        let request = request();

        for trial in find(&request)
        {
            assert!(check_trial(&request, &trial).is_empty(), "{}", trial.id());
        }
    }

    #[test]
    fn independent_checker_rejects_mutated_analysis_and_score()
    {
        let request = request();
        let trials = find(&request);
        let selected = pick(trials.iter()).unwrap();

        let mut bad_range = selected.clone();
        bad_range.analysis.accumulator_bound += 1;
        assert!(check_plan(&request, bad_range.analysis()).contains(&"bad range".into()));

        let mut bad_score = selected.clone();
        bad_score.score.cost += 1;
        assert!(check_trial(&request, &bad_score).contains(&"bad score".into()));

        let mut bad_ram = selected.clone();
        bad_ram.analysis.temporary_bytes += 1;
        assert!(check_plan(&request, bad_ram.analysis()).contains(&"bad ram".into()));

        let mut bad_alias = selected.clone();
        bad_alias.analysis.alias_safe = !bad_alias.analysis.alias_safe;
        assert!(check_plan(&request, bad_alias.analysis()).contains(&"bad alias flag".into()));

        let mut bad_operations = selected.clone();
        bad_operations.analysis.multiplications += 1;
        assert!(check_plan(&request, bad_operations.analysis()).contains(&"bad op count".into()));

        let mut bad_legality = selected.clone();
        bad_legality.analysis.legal = false;
        assert!(check_plan(&request, bad_legality.analysis()).contains(&"bad legality".into()));
    }

    #[test]
    fn checker_rejects_invalid_plan_parameters_without_panicking()
    {
        let request = request();
        let mut trial = find(&request).remove(0);
        trial.analysis.plan.block_size = 4;
        trial.analysis.plan.accumulator_bits = 0;
        let errors = check_trial(&request, &trial);

        assert!(errors.contains(&"unexpected block".into()));
        assert!(errors.contains(&"bad acc type".into()));
    }
}
