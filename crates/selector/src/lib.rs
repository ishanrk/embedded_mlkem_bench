mod bounds;
mod check;
mod plan;
mod search;
mod spec;

pub use bounds::{accumulator_bound, product_bound, required_signed_bits, signed_width_fits};
pub use check::{check_plan, check_trial};
pub use plan::{AnalysisVerdict, CandidateTrial, Schedule, SchoolbookPlan, StaticScore};
pub use search::{SelectionError, analyze, find, frontier, generate_candidates, pick};
pub use search::generate_candidates as r#gen;
pub use spec::{
    Aliasing, InputRepresentation, Limits, Operation, OutputRepresentation, Request, SpecError,
    Target, load_request, parse_request,
};
