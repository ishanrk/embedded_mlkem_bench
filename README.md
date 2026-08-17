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

`limits.ram` has the same narrow meaning: maximum explicit scratch generated
inside the selected kernel. PicoRV32 measurements keep caller working storage,
compiler frames, proved call-chain bounds, runtime stack high water, text,
read-only data, initialized data, and BSS in separate fields. They are never
folded into an unexplained total RAM number.

## Repository layout

- `src/selector.cpp`: parse requests, enumerate plans, enforce bounds, and rank;
- `src/codegen.cpp`: emit one standalone kernel from the selected plan;
- `src/host_tuner.cpp`: compile, test, and measure generated kernels locally;
- `src/tuning.cpp`: validate benchmark records and serialize results;
- `src/target_measurement.cpp`: parse target cycles, stack, ELF, manifest, and synthesis data;
- `src/explore.cpp`: CLI parsing and artifact orchestration;
- `src/formula/`: fixed NTRU-509 formula experiments;
- `targets/picorv32/`: opt-in firmware, RTL simulation, and ECP5 synthesis flow;
- `tests/`: selector, generator, tuner, CLI, ring, and formula tests.

## PicoRV32 step 1

The opt-in target flow pins PicoRV32 at
`a473fc8fca393771d83b0ffcf0b14db3393339d8` with an archive hash, builds bare
metal RV32IMC smoke firmware, and provides a project PCPI implementation of the
four standard multiply instructions. PicoRV32 remains unmodified and supplies
division. The project multiplier has the same two-clock issue-to-ready latency as
PicoRV32's stock fast multiplier. The Verilator harness models a deterministic
one-cycle memory response and records benchmark marker handshakes rather than
using host or ISS timing.

The target flow is excluded from normal host presets and performs no download
unless explicitly enabled:

```bash
cmake --preset picorv32-sim
cmake --build --preset picorv32-sim

cmake --preset picorv32-synthesis
cmake --build --preset picorv32-synthesis
cmake --build build/picorv32-synthesis --target pqc-picorv32-finalize
```

The simulation preset requires `riscv32-unknown-elf-gcc`, `ld`, `objcopy`,
`objdump`, and `size` from RISC-V GNU toolchain release `2026.07.15`, plus
Verilator from OSS CAD Suite `2026-07-29`. Synthesis additionally requires
Yosys, `nextpnr-ecp5`, `ecppack`, and Z3 from that CAD release. Missing tools
cause configuration to fail with the exact executable and required release;
the build never installs a toolchain.

When those pinned tools are present the simulation target builds project and
stock-fast-multiplier configurations, checks deterministic repeated cycles,
runs trap firmware, calibrates marker overhead, scans a separate measured stack,
and records final ELF section sizes. The synthesis target retains seeds 1 through
5 for `LFE5U-45F-6BG381C` at 50 MHz and fails if any seed misses timing.
The explicit finalize target copies only a fully completed compact experiment to
`results/raw/picorv32-step1-<repository>-<picorv32>/`.

The completed local Step 1 run measured 3249 calibrated cycles for both the
project and stock multipliers. It used 32 bytes of measured stack high water and
1288 allocated flash bytes. All five synthesis seeds used 3583 LUT4, 970 flip
flops, 4 DSP blocks, and no block RAM; routed frequencies ranged from 66.39 to
70.68 MHz. These are substrate measurements for the fixed smoke workload, not
ML-KEM results. ML-KEM schedules and `mlk.fqmul` belong to later steps and are
intentionally absent.

## Verification limits

A measured candidate is selectable only when plan consistency, scratch limits,
differential tests, and ASan/UBSan all pass. This is useful test coverage, not a
formal proof. The project does not currently run CBMC or prove constant-time
execution. Host measurements remain development proxies; only completed external
RTL simulation may supply PicoRV32 cycle claims.

After every Step 1 gate passes with the pinned tools, Step 2 can add complete
software ML-KEM schedules and run the full software comparison. The custom
`mlk.fqmul` instruction remains deferred until that comparison exists.
