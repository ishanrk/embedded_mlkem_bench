use crate::bounds::{accumulator_bound, required_signed_bits, signed_width_fits};
use crate::{Aliasing, AnalysisVerdict, CandidateTrial, Request, Schedule, SchoolbookPlan, StaticScore};
use std::collections::BTreeSet;
use std::fmt;

pub fn generate_candidates(request: &Request) -> Vec<SchoolbookPlan>
{
    let mut candidates = Vec::new();

    for accumulator_bits in request.target().accumulator_bits()
    {
        candidates.push(SchoolbookPlan::new(Schedule::Full, *accumulator_bits, 0));

        let mut block_sizes = BTreeSet::new();
        for block_size in [4, 8, 16, 32, request.coefficient_count()]
        {
            block_sizes.insert(request.coefficient_count().min(block_size));
        }
        for block_size in block_sizes
        {
            candidates.push(SchoolbookPlan::new(
                Schedule::Fold,
                *accumulator_bits,
                block_size,
            ));
        }

        candidates.push(SchoolbookPlan::new(Schedule::Output, *accumulator_bits, 0));
    }

    candidates
}

fn temporary_bytes(request: &Request, plan: &SchoolbookPlan) -> u128
{
    let coefficient_count = u128::from(request.coefficient_count());
    let accumulator_bytes = u128::from(plan.accumulator_bits / 8);

    match plan.schedule
    {
        Schedule::Full => (2 * coefficient_count - 1) * accumulator_bytes,
        Schedule::Fold => coefficient_count * accumulator_bytes,
        Schedule::Output => 0,
    }
}

fn alias_safe(plan: &SchoolbookPlan) -> bool
{
    plan.schedule != Schedule::Output
}

fn target_size_is_legal(request: &Request, plan: &SchoolbookPlan) -> bool
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

    let accumulator_bytes = u128::from(plan.accumulator_bits / 8);
    match plan.schedule
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

fn operation_counts(request: &Request, plan: &SchoolbookPlan) -> (u128, u128, u128)
{
    let coefficient_count = u128::from(request.coefficient_count());
    let multiplications = coefficient_count * coefficient_count;
    let additions = if plan.schedule == Schedule::Full
    {
        multiplications + coefficient_count - 1
    }
    else
    {
        multiplications
    };

    (multiplications, additions, coefficient_count)
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

fn estimated_cost(
    request: &Request,
    plan: &SchoolbookPlan,
    multiplications: u128,
    additions: u128,
    reductions: u128,
) -> u128
{
    let wide = plan.accumulator_bits > request.target().word_bits();
    let mut cost = checked_cost_multiply(if wide { 10 } else { 4 }, multiplications);
    cost = checked_cost_add(cost, additions);
    cost = checked_cost_add(
        cost,
        checked_cost_multiply(if wide { 14 } else { 8 }, reductions),
    );

    match plan.schedule
    {
        Schedule::Fold =>
        {
            let coefficient_count = u128::from(request.coefficient_count());
            let block_size = u128::from(plan.block_size);
            let tiles = coefficient_count.div_ceil(block_size);
            cost = checked_cost_add(cost, multiplications / 4);
            cost = checked_cost_add(cost, checked_cost_multiply(2, checked_cost_multiply(tiles, tiles)));
        }
        Schedule::Output =>
        {
            cost = checked_cost_add(cost, multiplications / 2);
        }
        Schedule::Full => {}
    }

    cost
}

pub fn analyze(request: &Request, plan: &SchoolbookPlan) -> CandidateTrial
{
    let temporary_bytes = temporary_bytes(request, plan);
    let accumulator_bound = accumulator_bound(request);
    let required_bits = required_signed_bits(accumulator_bound);
    let alias_safe = alias_safe(plan);
    let (multiplications, additions, reductions) = operation_counts(request, plan);
    let mut failure_reasons = Vec::new();

    if temporary_bytes > u128::from(request.limits().ram())
    {
        failure_reasons.push("ram".into());
    }
    if !signed_width_fits(accumulator_bound, plan.accumulator_bits)
    {
        failure_reasons.push("acc_width".into());
    }
    if request.aliasing() == Aliasing::May && !alias_safe
    {
        failure_reasons.push("alias".into());
    }
    if !target_size_is_legal(request, plan)
    {
        failure_reasons.push("size_t".into());
    }

    CandidateTrial
    {
        analysis: AnalysisVerdict
        {
            plan: plan.clone(),
            temporary_bytes,
            alias_safe,
            accumulator_bound,
            required_bits,
            multiplications,
            additions,
            reductions,
            legal: failure_reasons.is_empty(),
            failure_reasons,
        },
        score: StaticScore
        {
            cost: estimated_cost(request, plan, multiplications, additions, reductions),
            model: "starter-v0".into(),
        },
    }
}

pub fn find(request: &Request) -> Vec<CandidateTrial>
{
    generate_candidates(request)
        .iter()
        .map(|candidate| analyze(request, candidate))
        .collect()
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct SelectionError;

impl fmt::Display for SelectionError
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result
    {
        formatter.write_str("no legal plan")
    }
}

impl std::error::Error for SelectionError {}

pub fn pick<'a, I>(candidates: I) -> Result<&'a CandidateTrial, SelectionError>
where
    I: IntoIterator<Item = &'a CandidateTrial>,
{
    candidates
        .into_iter()
        .filter(|candidate| candidate.legal())
        .min_by_key(|candidate| {
            (
                candidate.estimated_cost(),
                candidate.temporary_bytes(),
                candidate.id(),
            )
        })
        .ok_or(SelectionError)
}

pub fn frontier<'a, I>(candidates: I) -> Vec<&'a CandidateTrial>
where
    I: IntoIterator<Item = &'a CandidateTrial>,
{
    let legal: Vec<_> = candidates
        .into_iter()
        .filter(|candidate| candidate.legal())
        .collect();
    let mut frontier: Vec<_> = legal
        .iter()
        .copied()
        .filter(|candidate| {
            !legal.iter().copied().any(|other| {
                other.temporary_bytes() <= candidate.temporary_bytes()
                    && other.estimated_cost() <= candidate.estimated_cost()
                    && (other.temporary_bytes() < candidate.temporary_bytes()
                        || other.estimated_cost() < candidate.estimated_cost())
            })
        })
        .collect();

    frontier.sort_by_key(|candidate| {
        (
            candidate.temporary_bytes(),
            candidate.estimated_cost(),
            candidate.id(),
        )
    });
    frontier
}

#[cfg(test)]
mod tests
{
    use super::*;
    use crate::parse_request;

    fn request(
        coefficient_count: u64,
        modulus: u32,
        ram: u64,
        input: &str,
        alias: &str,
        accumulator_bits: &str,
    ) -> Request
    {
        parse_request(&format!(
            r#"{{"op":"negacyclic_mul","n":{coefficient_count},"q":{modulus},"input":"{input}","alias":"{alias}","target":{{"name":"test","word_bits":32,"acc_bits":{accumulator_bits}}},"limits":{{"ram":{ram}}}}}"#,
        ))
        .unwrap()
    }

    #[test]
    fn candidate_order_and_exact_ranking_are_deterministic()
    {
        let trials = find(&request(8, 17, 60, "centered", "no", "32"));
        let ids: Vec<_> = trials.iter().map(CandidateTrial::id).collect();
        let costs: Vec<_> = trials.iter().map(CandidateTrial::estimated_cost).collect();

        assert_eq!(
            ids,
            ["sb_full_i32", "sb_fold_b4_i32", "sb_fold_b8_i32", "sb_out_i32"],
        );
        assert_eq!(costs, [391, 408, 402, 416]);
        assert_eq!(pick(trials.iter()).unwrap().id(), "sb_full_i32");
        assert_eq!(pick(trials.iter().rev()).unwrap().id(), "sb_full_i32");

        let wanted = ["sb_out_i32", "sb_fold_b8_i32", "sb_full_i32"];
        assert_eq!(
            frontier(trials.iter()).iter().map(|candidate| candidate.id()).collect::<Vec<_>>(),
            wanted,
        );
        assert_eq!(
            frontier(trials.iter().rev()).iter().map(|candidate| candidate.id()).collect::<Vec<_>>(),
            wanted,
        );
    }

    #[test]
    fn accumulator_width_is_searched()
    {
        let trials = find(&request(256, 3329, 4096, "centered", "no", "[32,64]"));
        let selected = pick(trials.iter()).unwrap();

        assert_eq!(selected.id(), "sb_full_i32");
        assert_eq!(selected.accumulator_bits(), 32);
        assert_eq!(selected.temporary_bytes(), 2044);
    }

    #[test]
    fn size_t_limit_is_enforced_without_overflow()
    {
        let trials = find(&request(
            (1u64 << 30) + 1,
            2,
            0,
            "centered",
            "no",
            "64",
        ));

        assert!(trials.iter().all(|candidate| {
            candidate.failure_reasons().iter().any(|reason| reason == "size_t")
        }));
        assert_eq!(pick(trials.iter()), Err(SelectionError));
    }

    #[test]
    fn ram_edges_match_the_reference()
    {
        let cases = [
            (31, vec!["sb_out_i32"]),
            (32, vec!["sb_fold_b4_i32", "sb_fold_b8_i32", "sb_out_i32"]),
            (59, vec!["sb_fold_b4_i32", "sb_fold_b8_i32", "sb_out_i32"]),
            (
                60,
                vec!["sb_full_i32", "sb_fold_b4_i32", "sb_fold_b8_i32", "sb_out_i32"],
            ),
        ];

        for (ram, wanted) in cases
        {
            let trials = find(&request(8, 17, ram, "centered", "no", "32"));
            let got: Vec<_> = trials
                .iter()
                .filter(|candidate| candidate.legal())
                .map(CandidateTrial::id)
                .collect();

            assert_eq!(got, wanted);
        }
    }

    #[test]
    fn aliasing_prunes_only_the_output_schedule()
    {
        let trials = find(&request(8, 17, 1000, "centered", "may", "32"));
        let output = trials.iter().find(|candidate| candidate.schedule() == Schedule::Output).unwrap();

        assert!(!output.legal());
        assert_eq!(output.failure_reasons(), &[String::from("alias")]);
        assert!(trials.iter().filter(|candidate| candidate.legal()).all(CandidateTrial::alias_safe));
    }

    #[test]
    fn zero_ram_and_aliasing_has_no_legal_plan()
    {
        let trials = find(&request(8, 17, 0, "centered", "may", "32"));

        assert_eq!(pick(trials.iter()), Err(SelectionError));
    }

    #[test]
    fn accumulator_width_thresholds_are_exact()
    {
        for (input, legal_count, illegal_count) in
            [("canonical", 193, 194), ("centered", 775, 776)]
        {
            let legal = find(&request(legal_count, 3329, 20_000, input, "no", "32"));
            let illegal = find(&request(illegal_count, 3329, 20_000, input, "no", "32"));

            assert!(legal.iter().any(CandidateTrial::legal));
            assert!(illegal.iter().all(|candidate| {
                candidate.failure_reasons().iter().any(|reason| reason == "acc_width")
            }));
        }
    }

    #[test]
    fn cyclic_and_negacyclic_requests_have_identical_analysis_costs()
    {
        let negacyclic = request(8, 17, 1000, "centered", "no", "32");
        let cyclic = parse_request(
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":{"acc_bits":32},"limits":{"ram":1000}}"#,
        )
        .unwrap();

        assert_eq!(find(&negacyclic), find(&cyclic));
    }

    #[test]
    fn maximum_supported_request_size_is_analyzed_without_integer_overflow()
    {
        let request = request(u64::MAX / 4, 2, 0, "centered", "no", "64");
        let trials = find(&request);

        assert!(!trials.is_empty());
        assert!(trials.iter().all(|candidate| {
            candidate.failure_reasons().iter().any(|reason| reason == "size_t")
        }));
        assert!(trials.iter().all(|candidate| !candidate.legal()));
    }

    #[test]
    fn target_object_size_limit_is_enforced()
    {
        let maximum_coefficients = (i32::MAX as u64) / 4;
        let legal = find(&request(
            maximum_coefficients,
            2,
            0,
            "centered",
            "no",
            "32",
        ));
        let illegal = find(&request(
            maximum_coefficients + 1,
            2,
            0,
            "centered",
            "no",
            "32",
        ));

        assert!(legal.iter().any(|candidate| {
            candidate.schedule() == Schedule::Output && candidate.legal()
        }));
        assert!(illegal.iter().all(|candidate| {
            candidate.failure_reasons().iter().any(|reason| reason == "size_t")
        }));
    }

    #[test]
    fn oversized_scratch_cannot_leave_an_alias_safe_plan_legal()
    {
        let request = parse_request(
            r#"{
                "op":"cyclic_mul",
                "n":1200000000000000000,
                "q":2,
                "alias":"may",
                "target":{"word_bits":64,"size_bits":64,"acc_bits":64},
                "limits":{"ram":10000000000000000000}
            }"#,
        )
        .unwrap();
        let trials = find(&request);

        assert!(trials.iter().filter(|candidate| candidate.alias_safe()).all(|candidate| {
            candidate.failure_reasons().iter().any(|reason| reason == "size_t")
        }));
        assert_eq!(pick(trials.iter()), Err(SelectionError));
    }
}
