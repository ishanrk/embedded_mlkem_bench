use serde::{Deserialize, Deserializer, Serialize};
use std::fmt;
use std::path::Path;

const MAX_COEFFICIENT_COUNT: u64 = u64::MAX / 4;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Operation
{
    NegacyclicMul,
    CyclicMul,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum InputRepresentation
{
    Centered,
    Canonical,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum OutputRepresentation
{
    Canonical,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Aliasing
{
    No,
    May,
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct Target
{
    name: String,
    word_bits: u16,
    size_bits: u16,
    acc_bits: Vec<u16>,
}

impl Target
{
    pub fn name(&self) -> &str
    {
        &self.name
    }

    pub fn word_bits(&self) -> u16
    {
        self.word_bits
    }

    pub fn size_bits(&self) -> u16
    {
        self.size_bits
    }

    pub fn accumulator_bits(&self) -> &[u16]
    {
        &self.acc_bits
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct Limits
{
    ram: u64,
}

impl Limits
{
    pub fn ram(&self) -> u64
    {
        self.ram
    }
}

#[derive(Clone, Debug, Eq, PartialEq, Serialize)]
pub struct Request
{
    op: Operation,
    n: u64,
    q: u32,
    input: InputRepresentation,
    output: OutputRepresentation,
    alias: Aliasing,
    target: Target,
    limits: Limits,
}

impl Request
{
    pub fn operation(&self) -> Operation
    {
        self.op
    }

    pub fn coefficient_count(&self) -> u64
    {
        self.n
    }

    pub fn modulus(&self) -> u32
    {
        self.q
    }

    pub fn input_representation(&self) -> InputRepresentation
    {
        self.input
    }

    pub fn output_representation(&self) -> OutputRepresentation
    {
        self.output
    }

    pub fn aliasing(&self) -> Aliasing
    {
        self.alias
    }

    pub fn target(&self) -> &Target
    {
        &self.target
    }

    pub fn limits(&self) -> &Limits
    {
        &self.limits
    }

    pub fn input_lower_bound(&self) -> i64
    {
        match self.input
        {
            InputRepresentation::Canonical => 0,
            InputRepresentation::Centered => -(i64::from(self.q) / 2),
        }
    }

    pub fn input_upper_bound(&self) -> i64
    {
        match self.input
        {
            InputRepresentation::Canonical => i64::from(self.q) - 1,
            InputRepresentation::Centered => (i64::from(self.q) - 1) / 2,
        }
    }

    pub fn input_bound(&self) -> u64
    {
        self.input_lower_bound()
            .unsigned_abs()
            .max(self.input_upper_bound().unsigned_abs())
    }

    #[allow(clippy::too_many_arguments)]
    pub fn from_parts(
        operation: Operation,
        coefficient_count: u64,
        modulus: u32,
        input_representation: InputRepresentation,
        output_representation: OutputRepresentation,
        aliasing: Aliasing,
        target_name: String,
        target_word_bits: u16,
        target_size_bits: u16,
        target_accumulator_bits: Vec<u16>,
        ram: u64,
    ) -> Result<Self, SpecError>
    {
        Self::from_wire(RequestWire
        {
            op: operation,
            n: coefficient_count,
            q: modulus,
            input: input_representation,
            output: output_representation,
            alias: aliasing,
            target: TargetWire
            {
                name: target_name,
                word_bits: target_word_bits,
                size_bits: target_size_bits,
                acc_bits: target_accumulator_bits,
            },
            limits: LimitsWire { ram },
        })
    }

    fn from_wire(mut wire: RequestWire) -> Result<Self, SpecError>
    {
        wire.target.acc_bits.sort_unstable();
        wire.target.acc_bits.dedup();

        let request = Self
        {
            op: wire.op,
            n: wire.n,
            q: wire.q,
            input: wire.input,
            output: wire.output,
            alias: wire.alias,
            target: Target
            {
                name: wire.target.name,
                word_bits: wire.target.word_bits,
                size_bits: wire.target.size_bits,
                acc_bits: wire.target.acc_bits,
            },
            limits: Limits { ram: wire.limits.ram },
        };

        request.validate()?;
        Ok(request)
    }

    pub fn validate(&self) -> Result<(), SpecError>
    {
        if self.n < 2
        {
            return Err(SpecError::new("n must be at least 2"));
        }
        if self.n > MAX_COEFFICIENT_COUNT
        {
            return Err(SpecError::new("n is too large for exact analysis"));
        }
        if self.q < 2 || self.q > i32::MAX as u32
        {
            return Err(SpecError::new("q must fit a positive int32_t"));
        }
        if self.target.name.is_empty()
        {
            return Err(SpecError::new("target name must be a nonempty string"));
        }
        if !(8..=64).contains(&self.target.word_bits)
        {
            return Err(SpecError::new("word_bits must be in [8, 64]"));
        }
        if !matches!(self.target.size_bits, 32 | 64)
        {
            return Err(SpecError::new("size_bits must be 32 or 64"));
        }
        if self.target.acc_bits.is_empty()
        {
            return Err(SpecError::new("acc_bits must be nonempty"));
        }
        if self.target.acc_bits.iter().any(|bits| !matches!(bits, 32 | 64))
        {
            return Err(SpecError::new("acc_bits entries must be 32 or 64 for now"));
        }
        if self.target.acc_bits.windows(2).any(|pair| pair[0] >= pair[1])
        {
            return Err(SpecError::new("acc_bits must be sorted and unique"));
        }

        let signed_lower = -(1i128 << (self.target.word_bits - 1));
        let signed_upper = (1i128 << (self.target.word_bits - 1)) - 1;
        if i128::from(self.input_lower_bound()) < signed_lower
            || i128::from(self.input_upper_bound()) > signed_upper
        {
            return Err(SpecError::new("input representatives do not fit target word_bits"));
        }

        Ok(())
    }
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SpecError
{
    message: String,
}

impl SpecError
{
    fn new(message: impl Into<String>) -> Self
    {
        Self { message: message.into() }
    }
}

impl fmt::Display for SpecError
{
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result
    {
        formatter.write_str(&self.message)
    }
}

impl std::error::Error for SpecError {}

fn default_input() -> InputRepresentation
{
    InputRepresentation::Centered
}

fn default_output() -> OutputRepresentation
{
    OutputRepresentation::Canonical
}

fn default_aliasing() -> Aliasing
{
    Aliasing::No
}

fn default_target_name() -> String
{
    "host".into()
}

fn default_word_bits() -> u16
{
    32
}

fn default_size_bits() -> u16
{
    32
}

fn default_accumulator_bits() -> Vec<u16>
{
    vec![32, 64]
}

#[derive(Deserialize)]
#[serde(untagged)]
enum AccumulatorBitsWire
{
    One(u16),
    Many(Vec<u16>),
}

fn deserialize_accumulator_bits<'de, D>(deserializer: D) -> Result<Vec<u16>, D::Error>
where
    D: Deserializer<'de>,
{
    let wire = AccumulatorBitsWire::deserialize(deserializer)?;
    let mut bits = match wire
    {
        AccumulatorBitsWire::One(bits) => vec![bits],
        AccumulatorBitsWire::Many(bits) => bits,
    };

    bits.sort_unstable();
    bits.dedup();
    Ok(bits)
}

#[derive(Deserialize)]
#[serde(default, deny_unknown_fields)]
struct TargetWire
{
    #[serde(default = "default_target_name")]
    name: String,
    #[serde(default = "default_word_bits")]
    word_bits: u16,
    #[serde(default = "default_size_bits")]
    size_bits: u16,
    #[serde(default = "default_accumulator_bits", deserialize_with = "deserialize_accumulator_bits")]
    acc_bits: Vec<u16>,
}

impl Default for TargetWire
{
    fn default() -> Self
    {
        Self
        {
            name: default_target_name(),
            word_bits: default_word_bits(),
            size_bits: default_size_bits(),
            acc_bits: default_accumulator_bits(),
        }
    }
}

#[derive(Default, Deserialize)]
#[serde(default, deny_unknown_fields)]
struct LimitsWire
{
    ram: u64,
}

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct RequestWire
{
    op: Operation,
    n: u64,
    q: u32,
    #[serde(default = "default_input")]
    input: InputRepresentation,
    #[serde(default = "default_output")]
    output: OutputRepresentation,
    #[serde(default = "default_aliasing")]
    alias: Aliasing,
    #[serde(default)]
    target: TargetWire,
    #[serde(default)]
    limits: LimitsWire,
}

pub fn parse_request(json: &str) -> Result<Request, SpecError>
{
    let value: serde_json::Value = serde_json::from_str(json)
        .map_err(|error| SpecError::new(format!("invalid request: {error}")))?;
    validate_wire_shape(&value)?;
    let wire: RequestWire = serde_json::from_value(value)
        .map_err(|error| SpecError::new(format!("invalid request: {error}")))?;

    Request::from_wire(wire)
}

fn validate_wire_shape(value: &serde_json::Value) -> Result<(), SpecError>
{
    const REQUEST_FIELDS: &[&str] =
        &["op", "n", "q", "input", "output", "alias", "target", "limits"];
    const TARGET_FIELDS: &[&str] = &["name", "word_bits", "size_bits", "acc_bits"];
    const LIMIT_FIELDS: &[&str] = &["ram"];

    let request = value
        .as_object()
        .ok_or_else(|| SpecError::new("request must be an object"))?;
    reject_unknown_fields(request, REQUEST_FIELDS, "request")?;

    let empty = serde_json::Map::new();
    let target = request
        .get("target")
        .map(serde_json::Value::as_object)
        .unwrap_or(Some(&empty));
    let limits = request
        .get("limits")
        .map(serde_json::Value::as_object)
        .unwrap_or(Some(&empty));
    let (Some(target), Some(limits)) = (target, limits)
    else
    {
        return Err(SpecError::new("target and limits must be objects"));
    };

    reject_unknown_fields(target, TARGET_FIELDS, "target")?;
    reject_unknown_fields(limits, LIMIT_FIELDS, "limits")?;
    Ok(())
}

fn reject_unknown_fields(
    object: &serde_json::Map<String, serde_json::Value>,
    allowed: &[&str],
    location: &str,
) -> Result<(), SpecError>
{
    let mut unknown: Vec<_> = object
        .keys()
        .filter(|field| !allowed.contains(&field.as_str()))
        .map(String::as_str)
        .collect();
    unknown.sort_unstable();

    if unknown.is_empty()
    {
        Ok(())
    }
    else
    {
        Err(SpecError::new(format!(
            "unknown {location} field: {}",
            unknown.join(", "),
        )))
    }
}

pub fn load_request(path: impl AsRef<Path>) -> Result<Request, SpecError>
{
    let path = path.as_ref();
    let json = std::fs::read_to_string(path)
        .map_err(|error| SpecError::new(format!("could not read {}: {error}", path.display())))?;

    parse_request(&json)
}

#[cfg(test)]
mod tests
{
    use super::*;

    fn base(extra: &str) -> String
    {
        format!(r#"{{"op":"negacyclic_mul","n":8,"q":17,"limits":{{"ram":1000}}{extra}}}"#)
    }

    #[test]
    fn request_defaults_match_the_reference()
    {
        let request = parse_request(&base("")).unwrap();

        assert_eq!(request.operation(), Operation::NegacyclicMul);
        assert_eq!(request.input_representation(), InputRepresentation::Centered);
        assert_eq!(request.output_representation(), OutputRepresentation::Canonical);
        assert_eq!(request.aliasing(), Aliasing::No);
        assert_eq!(request.target().accumulator_bits(), &[32, 64]);
        assert_eq!(request.input_bound(), 8);
        assert_eq!((request.input_lower_bound(), request.input_upper_bound()), (-8, 8));
    }

    #[test]
    fn even_centered_range_is_asymmetric()
    {
        let request = parse_request(
            r#"{"op":"cyclic_mul","n":8,"q":256,"target":{"word_bits":16,"acc_bits":32}}"#,
        )
        .unwrap();

        assert_eq!((request.input_lower_bound(), request.input_upper_bound()), (-128, 127));
        assert_eq!(request.input_bound(), 128);
    }

    #[test]
    fn accumulator_widths_accept_an_integer_and_normalize_a_list()
    {
        let one = parse_request(
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":{"acc_bits":32}}"#,
        )
        .unwrap();
        let many = parse_request(
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":{"acc_bits":[64,32,64]}}"#,
        )
        .unwrap();

        assert_eq!(one.target().accumulator_bits(), &[32]);
        assert_eq!(many.target().accumulator_bits(), &[32, 64]);
    }

    #[test]
    fn invalid_values_and_unknown_fields_are_rejected()
    {
        let invalid = [
            r#"[]"#,
            r#"{"op":"cyclic_mul","n":1,"q":17}"#,
            r#"{"op":"cyclic_mul","n":8,"q":1}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"input":"wide"}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"output":"centered"}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"alias":"sometimes"}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":[]}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"limits":[]}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":["test",32,32,[32]]}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"limits":[1000]}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":{"acc_bits":48}}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"limits":{"ram":-1}}"#,
            r#"{"op":"cyclic_mul","n":true,"q":17}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"ran":20}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"target":{"acc_bit":32}}"#,
            r#"{"op":"cyclic_mul","n":8,"q":17,"limits":{"ran":20}}"#,
        ];

        for json in invalid
        {
            assert!(parse_request(json).is_err(), "accepted {json}");
        }
    }

    #[test]
    fn unknown_fields_are_reported_together_in_sorted_order()
    {
        let error = parse_request(
            r#"{"op":"cyclic_mul","n":8,"q":17,"zeta":1,"alpha":2}"#,
        )
        .unwrap_err();

        assert_eq!(error.to_string(), "unknown request field: alpha, zeta");
    }

    #[test]
    fn duplicate_json_members_use_the_last_value()
    {
        let request = parse_request(
            r#"{"op":"negacyclic_mul","op":"cyclic_mul","n":4,"n":8,"q":17}"#,
        )
        .unwrap();

        assert_eq!(request.operation(), Operation::CyclicMul);
        assert_eq!(request.coefficient_count(), 8);
    }

    #[test]
    fn request_serialization_preserves_the_wire_keys()
    {
        let request = parse_request(&base("")).unwrap();
        let value = serde_json::to_value(request).unwrap();

        assert_eq!(value["op"], "negacyclic_mul");
        assert_eq!(value["n"], 8);
        assert_eq!(value["q"], 17);
        assert_eq!(value["target"]["acc_bits"], serde_json::json!([32, 64]));
        assert!(value.get("coefficient_count").is_none());
    }

    #[test]
    fn direct_request_construction_is_checked()
    {
        let result = Request::from_parts(
            Operation::CyclicMul,
            0,
            17,
            InputRepresentation::Centered,
            OutputRepresentation::Canonical,
            Aliasing::No,
            "host".into(),
            32,
            32,
            vec![32, 64],
            0,
        );

        assert!(result.is_err());
    }

    #[test]
    fn requests_beyond_the_exact_analysis_range_are_rejected()
    {
        let result = Request::from_parts(
            Operation::CyclicMul,
            MAX_COEFFICIENT_COUNT + 1,
            2,
            InputRepresentation::Centered,
            OutputRepresentation::Canonical,
            Aliasing::No,
            "host".into(),
            64,
            64,
            vec![64],
            0,
        );

        assert_eq!(result.unwrap_err().to_string(), "n is too large for exact analysis");
    }
}
