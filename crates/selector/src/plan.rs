use serde::ser::SerializeStruct;
use serde::{Serialize, Serializer};

#[derive(Clone, Copy, Debug, Eq, Ord, PartialEq, PartialOrd)]
pub enum Schedule
{
    Full,
    Fold,
    Output,
}

impl Schedule
{
    pub fn as_str(self) -> &'static str
    {
        match self
        {
            Self::Full => "sb_full",
            Self::Fold => "sb_fold",
            Self::Output => "sb_out",
        }
    }
}

impl Serialize for Schedule
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        serializer.serialize_str(self.as_str())
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SchoolbookPlan
{
    pub schedule: Schedule,
    pub accumulator_bits: u16,
    pub block_size: u64,
}

impl SchoolbookPlan
{
    pub fn new(schedule: Schedule, accumulator_bits: u16, block_size: u64) -> Self
    {
        Self
        {
            schedule,
            accumulator_bits,
            block_size,
        }
    }

    pub fn id(&self) -> String
    {
        if self.block_size != 0
        {
            format!(
                "{}_b{}_i{}",
                self.schedule.as_str(),
                self.block_size,
                self.accumulator_bits,
            )
        }
        else
        {
            format!("{}_i{}", self.schedule.as_str(), self.accumulator_bits)
        }
    }

    pub fn schedule(&self) -> Schedule
    {
        self.schedule
    }

    pub fn schedule_name(&self) -> &'static str
    {
        self.schedule.as_str()
    }

    pub fn accumulator_bits(&self) -> u16
    {
        self.accumulator_bits
    }

    pub fn block_size(&self) -> u64
    {
        self.block_size
    }
}

impl Serialize for SchoolbookPlan
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let mut state = serializer.serialize_struct("SchoolbookPlan", 5)?;
        state.serialize_field("id", &self.id())?;
        state.serialize_field("algo", "schoolbook")?;
        state.serialize_field("sched", &self.schedule)?;
        state.serialize_field("acc_bits", &self.accumulator_bits)?;
        state.serialize_field("block", &self.block_size)?;
        state.end()
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct AnalysisVerdict
{
    #[serde(skip_serializing)]
    pub plan: SchoolbookPlan,
    #[serde(rename = "tmp_bytes")]
    pub temporary_bytes: u128,
    pub alias_safe: bool,
    #[serde(rename = "acc_bound")]
    pub accumulator_bound: u128,
    #[serde(rename = "need_bits")]
    pub required_bits: u16,
    #[serde(rename = "muls")]
    pub multiplications: u128,
    #[serde(rename = "adds")]
    pub additions: u128,
    #[serde(rename = "reds")]
    pub reductions: u128,
    pub legal: bool,
    #[serde(rename = "fail")]
    pub failure_reasons: Vec<String>,
}

impl AnalysisVerdict
{
    pub fn id(&self) -> String
    {
        self.plan.id()
    }

    pub fn schedule(&self) -> Schedule
    {
        self.plan.schedule
    }

    pub fn block_size(&self) -> u64
    {
        self.plan.block_size
    }

    pub fn accumulator_bits(&self) -> u16
    {
        self.plan.accumulator_bits
    }

    pub fn failure_reasons(&self) -> &[String]
    {
        &self.failure_reasons
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct StaticScore
{
    pub cost: u128,
    pub model: String,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct CandidateTrial
{
    pub analysis: AnalysisVerdict,
    pub score: StaticScore,
}

impl CandidateTrial
{
    pub fn plan(&self) -> &SchoolbookPlan
    {
        &self.analysis.plan
    }

    pub fn analysis(&self) -> &AnalysisVerdict
    {
        &self.analysis
    }

    pub fn score(&self) -> &StaticScore
    {
        &self.score
    }

    pub fn id(&self) -> String
    {
        self.analysis.id()
    }

    pub fn schedule(&self) -> Schedule
    {
        self.analysis.schedule()
    }

    pub fn block_size(&self) -> u64
    {
        self.analysis.block_size()
    }

    pub fn accumulator_bits(&self) -> u16
    {
        self.analysis.accumulator_bits()
    }

    pub fn temporary_bytes(&self) -> u128
    {
        self.analysis.temporary_bytes
    }

    pub fn alias_safe(&self) -> bool
    {
        self.analysis.alias_safe
    }

    pub fn accumulator_bound(&self) -> u128
    {
        self.analysis.accumulator_bound
    }

    pub fn required_bits(&self) -> u16
    {
        self.analysis.required_bits
    }

    pub fn multiplications(&self) -> u128
    {
        self.analysis.multiplications
    }

    pub fn additions(&self) -> u128
    {
        self.analysis.additions
    }

    pub fn reductions(&self) -> u128
    {
        self.analysis.reductions
    }

    pub fn legal(&self) -> bool
    {
        self.analysis.legal
    }

    pub fn failure_reasons(&self) -> &[String]
    {
        self.analysis.failure_reasons()
    }

    pub fn estimated_cost(&self) -> u128
    {
        self.score.cost
    }

    pub fn cost_model(&self) -> &str
    {
        &self.score.model
    }
}

impl Serialize for CandidateTrial
{
    fn serialize<S>(&self, serializer: S) -> Result<S::Ok, S::Error>
    where
        S: Serializer,
    {
        let mut state = serializer.serialize_struct("CandidateTrial", 3)?;
        state.serialize_field("plan", self.plan())?;
        state.serialize_field("analysis", &self.analysis)?;
        state.serialize_field("score", &self.score)?;
        state.end()
    }
}

#[cfg(test)]
mod tests
{
    use super::*;

    #[test]
    fn plan_ids_and_wire_keys_match_the_reference()
    {
        let plan = SchoolbookPlan::new(Schedule::Fold, 64, 16);
        let value = serde_json::to_value(&plan).unwrap();

        assert_eq!(plan.id(), "sb_fold_b16_i64");
        assert_eq!(
            value,
            serde_json::json!({
                "id": "sb_fold_b16_i64",
                "algo": "schoolbook",
                "sched": "sb_fold",
                "acc_bits": 64,
                "block": 16,
            }),
        );
    }

    #[test]
    fn trial_serialization_does_not_duplicate_the_plan_inside_analysis()
    {
        let trial = CandidateTrial
        {
            analysis: AnalysisVerdict
            {
                plan: SchoolbookPlan::new(Schedule::Output, 32, 0),
                temporary_bytes: 0,
                alias_safe: false,
                accumulator_bound: 12,
                required_bits: 5,
                multiplications: 16,
                additions: 16,
                reductions: 4,
                legal: true,
                failure_reasons: Vec::new(),
            },
            score: StaticScore { cost: 100, model: "starter-v0".into() },
        };
        let value = serde_json::to_value(trial).unwrap();

        assert_eq!(value["plan"]["id"], "sb_out_i32");
        assert!(value["analysis"].get("plan").is_none());
        assert_eq!(value["analysis"]["tmp_bytes"], 0);
        assert_eq!(value["analysis"]["fail"], serde_json::json!([]));
        assert_eq!(value["score"]["model"], "starter-v0");
    }
}
