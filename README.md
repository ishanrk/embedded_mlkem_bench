# pqc-poly-bench

`pqc-poly-bench` selects and emits small C++20 kernels for cyclic and negacyclic
polynomial multiplication. A JSON request supplies the ring, coefficient range,
aliasing contract, target integer widths, and scratch-RAM limit. The tool rejects
plans that violate those constraints and either uses a static cost model or
measures the legal plans on the host.

This is an early compiler/autotuner prototype, not a complete PQC implementation.

## What works

The generic path supports three schoolbook schedules:

- `sb_full`: accumulate a linear convolution, then fold it into the ring;
- `sb_fold`: accumulate directly into the ring through fixed-size blocks;
- `sb_out`: compute each output coefficient without scratch storage.

For each supported 32- or 64-bit accumulator, the selector computes the signed
accumulator bound, operation counts, alias safety, and explicit scratch use. A
separate consistency pass recomputes those values before code generation.

The repository also contains fixed NTRU-HPS-2048-509 formula experiments
(schoolbook, Karatsuba, NTT, Toom-Cook, and TMVP). They are a separate benchmark:
the JSON selector does not choose between them, and its RAM limit does not apply
to their fixed internal storage.

## Build and test

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

For host-native formula benchmarks:

```bash
cmake --preset release-native
cmake --build --preset release-native --parallel
ctest --preset release-native
./build/release-native/pqc-poly-formula-bench 1000
```

For ASan and UBSan:

```bash
cmake --preset sanitize
cmake --build --preset sanitize --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize
```

## Use

Requests use one strict schema; duplicate or unknown fields are rejected:

```json
{
  "op": "negacyclic_mul",
  "n": 256,
  "q": 3329,
  "input": "centered",
  "output": "canonical",
  "alias": "no",
  "target": {
    "name": "rv32im",
    "word_bits": 32,
    "size_bits": 32,
    "acc_bits": [32, 64]
  },
  "limits": {"ram": 4096}
}
```

Static selection is deterministic and does not claim measured target performance:

```bash
./build/release/pqc-poly-bench examples/mlkem.json -o out
```

Use `--plan PLAN_ID` to force a legal candidate. Use host tuning to compile,
test, and measure every candidate locally:

```bash
./build/release/pqc-poly-bench \
  --tune-host \
  --metric nanoseconds \
  --samples 5 \
  --iterations 16 \
  examples/host-negacyclic.json \
  -o out
```

Host tuning runs deterministic differential tests under ASan and UBSan, builds
a separate `-O3` executable, records median latency and code size, and selects
the fastest passing candidate. Results are labeled `host-proxy-for-*`; they are
not results from the target named in a request.

## Output

A successful run writes:

- `kernel.hpp`, `kernel.cpp`: standalone C++20 with the `pqc_poly_mul` entry point;
- `plan.json`: the selected candidate and optional host measurement;
- `candidates.json`: every candidate, its analysis, score, and rejection reasons;
- `benchmarks.json`: checks, provenance, measurements, winner, and Pareto frontier.

`scratch_bytes` is the size of arrays emitted inside `pqc_poly_mul`. It does not
include input/output buffers, alignment or stack-frame overhead, compiler spills,
or the separate fixed-formula benchmark.

## Repository layout

- `src/selector.cpp`: parse requests, enumerate plans, enforce bounds, and rank;
- `src/codegen.cpp`: emit one standalone kernel from the selected plan;
- `src/host_tuner.cpp`: compile, test, and measure generated kernels locally;
- `src/tuning.cpp`: validate benchmark records and serialize results;
- `src/explore.cpp`: CLI parsing and artifact orchestration;
- `src/formula/`: fixed NTRU-509 formula experiments;
- `tests/`: selector, generator, tuner, CLI, ring, and formula tests.

## RISC-V status

There is no RISC-V instruction backend yet. Setting a target name such as
`rv32im` applies the requested `word_bits`, `size_bits`, accumulator widths, and
RAM limit to selection, but it does not cross-compile, run a simulator, emit
custom instructions, or measure RISC-V cycles. The generated kernel is portable
C++ intended for a later target backend.

A credible RISC-V backend still needs a pinned compiler triple and ABI, linker
script, simulator or board runner, machine-readable counters, and a defined
custom-instruction interface. Until those exist, host measurements remain only
a development proxy.

## Verification limits

A measured candidate is selectable only when plan consistency, scratch limits,
differential tests, and ASan/UBSan all pass. This is useful test coverage, not a
formal proof. The project does not currently run CBMC, prove constant-time
execution, include compiler-generated spill space in the RAM bound, or execute
on the requested target.

The next meaningful extensions are a real RV32 runner and additional generic
algorithms whose exact scratch usage is modeled by the selector. They should be
added only when they drive emitted code and tests, rather than as descriptive
metadata.
