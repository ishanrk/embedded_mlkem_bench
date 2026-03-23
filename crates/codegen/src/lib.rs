mod c;
mod rust;

use core::fmt;

use pqc_poly_selector::{check_plan, AnalysisVerdict, Operation, Request};

pub use c::generate_header;

#[derive(Clone, Debug, Eq, PartialEq)]
pub enum CodegenError
{
    BadPlan(Vec<String>),
    IllegalPlan,
}

impl fmt::Display for CodegenError
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result
    {
        match self
        {
            Self::BadPlan(errors) => write!(formatter, "bad plan: {}", errors.join(", ")),
            Self::IllegalPlan => formatter.write_str("cannot emit an illegal plan"),
        }
    }
}

impl std::error::Error for CodegenError
{
}

pub fn generate_c(
    request: &Request,
    verdict: &AnalysisVerdict,
) -> Result<String, CodegenError>
{
    validate_verdict(request, verdict)?;
    Ok(c::generate_source(request, verdict))
}

pub fn generate_rust(
    request: &Request,
    verdict: &AnalysisVerdict,
) -> Result<String, CodegenError>
{
    validate_verdict(request, verdict)?;
    Ok(rust::generate_source(request, verdict))
}

pub fn gen_h(request: &Request) -> String
{
    generate_header(request)
}

pub fn gen_c(
    request: &Request,
    verdict: &AnalysisVerdict,
) -> Result<String, CodegenError>
{
    generate_c(request, verdict)
}

pub fn gen_rust(
    request: &Request,
    verdict: &AnalysisVerdict,
) -> Result<String, CodegenError>
{
    generate_rust(request, verdict)
}

fn validate_verdict(
    request: &Request,
    verdict: &AnalysisVerdict,
) -> Result<(), CodegenError>
{
    let errors = check_plan(request, verdict);

    if !errors.is_empty()
    {
        return Err(CodegenError::BadPlan(errors));
    }
    if !verdict.legal
    {
        return Err(CodegenError::IllegalPlan);
    }

    Ok(())
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum WrapSign
{
    Add,
    Subtract,
}

fn wrap_sign(operation: Operation) -> WrapSign
{
    match operation
    {
        Operation::CyclicMul => WrapSign::Add,
        Operation::NegacyclicMul => WrapSign::Subtract,
    }
}

#[cfg(test)]
mod tests;
