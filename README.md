# pqc-poly-bench

`pqc-poly-bench` is a C++20 constraint-driven compiler and autotuner for
post-quantum polynomial arithmetic. Given a ring operation, degree, modulus,
target description, alias contract, and scratch-RAM limit, it constructs an
explainable implementation space, rejects illegal plans, independently checks
the selected lowering, and emits a standalone kernel.

The project is entirely C++. Cargo and Rust are not required.

## Current implementation slice

The current end-to-end path supports cyclic and negacyclic multiplication with
three verified schoolbook memory schedules:

- a full linear convolution buffer followed by ring folding;
- a blocked ring accumulator with selectable block size;
- a direct-output schedule with no scratch allocation.

It also implements the compiler infrastructure needed to grow beyond that
bootstrap space:

- a range-aware polynomial IR with exact signed coefficient intervals;
- reduction-state and required-width tracking;
- explicit operation dependencies and last-use information;
- aligned scratch regions with offsets and live ranges;
- structured algorithm trees for schoolbook, blocked schoolbook, Karatsuba,
  mixed Karatsuba, Toom-Cook, hybrid Toom/Karatsuba, NTT, and NTT+CRT;
- separate legality and lowering-support states, so an unimplemented family is
  never presented as runnable;
- an independent plan checker and a separate IR checker;
- verified-only empirical winner selection;
- measured latency/RAM/code-size Pareto frontiers;
- deterministic JSON artifacts and a standalone HTML report.

Recursive and transform families are already represented in `plans.json`, but
remain capability-blocked until their C++ lowerings, range proofs, and exact
workspace schedules exist.

## Architecture

- `pqc_poly_selector` validates requests and provides the conservative
  bootstrap search and bound model.
- `pqc_poly_compiler_plan` constructs algorithm trees and records recursion,
  leaves, reduction placement, and memory policy.
- `pqc_poly_ir` lowers supported plans to an explicit value/operation graph and
  independently verifies ranges, dependencies, wrap semantics, and lifetimes.
- `pqc_poly_codegen` emits standalone optimized C++20.
- `pqc_poly_tuning` validates benchmark records, selects only verified
  measurements, and computes a three-dimensional Pareto frontier.
- `pqc_poly_host_tuner` compiles each legal candidate twice: a sanitized
  differential-testing build and an optimized measurement build.
- `pqc_poly_explore` coordinates selection, emission, and reporting.
- `pqc_poly_formula` retains the allocation-free NTRU-HPS-2048-509 research
  kernels: schoolbook, Karatsuba, exact dual-prime NTT, Toom-Cook, and TMVP.

Public arithmetic APIs follow output-first `r, a, b` ordering. The C++ layout,
Allman braces, right-aligned pointers, lower snake case, and prefixed global
symbols are loosely based on
[mlkem-native](https://github.com/pq-code-package/mlkem-native). Source comments
are intentionally sparse, lowercase, and limited to design or safety rationale.

## Build and test

Portable optimized build:

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

Host-native optimized build and fixed-kernel benchmark:

```bash
cmake --preset release-native
cmake --build --preset release-native --parallel
ctest --preset release-native
./build/release-native/pqc-poly-formula-bench 1000
```

ASan and UBSan build:

```bash
cmake --preset sanitize
cmake --build --preset sanitize --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize
```

Leak detection is disabled in that command because LeakSanitizer cannot run
under the workspace's ptrace wrapper. AddressSanitizer and
UndefinedBehaviorSanitizer remain enabled.

## Static compilation

Static selection uses the conservative bootstrap cost model and does not claim
target measurements:

```bash
./build/release/pqc-poly-bench examples/mlkem.json -o out
```

Use `--plan PLAN_ID` to force a particular legal, supported bootstrap plan.

## Verified host autotuning

The first empirical backend is explicit because a host result must not be
confused with an RV32 result:

```bash
./build/release/pqc-poly-bench \
  --tune-host \
  --metric nanoseconds \
  --samples 5 \
  --iterations 16 \
  examples/host-negacyclic.json \
  -o out
```

For every legal supported candidate, this mode:

1. regenerates the checked kernel;
2. compiles and runs boundary, impulse, deterministic-random, and exact-alias
   differential tests under ASan and UBSan;
3. compiles a separate `-O3` kernel;
4. records median nanoseconds and, on x86, timestamp-counter ticks;
5. records `.text` size when the platform size tool is available;
6. chooses the fastest locally verified candidate for the requested metric;
7. reports nondominated latency, scratch, and code-size points.

Measurements are labeled `host-proxy-for-*`. They are not presented as target
cycles for a different architecture.

## Emitted artifacts

Every successful run writes:

- `kernel.hpp` and `kernel.cpp`: standalone optimized C++20;
- `plan.json`: selected lowering, analysis, compiler tree, verification state,
  and optional measurement;
- `cands.json`: bootstrap candidates, including rejection reasons;
- `plans.json`: the broader structured algorithm-tree space, including honest
  capability blockers;
- `ir.json`: ranges, widths, dependencies, reductions, storage, and lifetimes;
- `benchmarks.json`: provenance, verification gates, measurements, winner, and
  measured Pareto frontier;
- `report.html`: a standalone human-readable experiment report.

The generated ABI entry point is `pqc_poly_mul`. `kernel.hpp` also provides an
inline `polysel_mul` compatibility name without another generated function.

## Verification scope

A record is eligible for the current empirical selector only after:

- the original plan analysis is independently recomputed;
- the IR is independently recomputed and checked;
- differential tests pass;
- ASan and UBSan execution passes;
- the explicit scratch allocation fits the requested RAM budget.

This local gate is deliberately not called a CBMC proof or a real target run.
Those states remain `not_run` until the corresponding backend actually runs.
Generated loops and memory access are independent of coefficient values, but
the generic `% q` lowering is not itself a complete microarchitectural
constant-time proof. Power-of-two moduli use an unsigned mask.

## Fixed-kernel performance

The NTRU-509 formula backends perform no heap allocation. Scratch is fixed-size
and 64-byte aligned; the native schoolbook path uses AVX2; recursive algorithms
reuse one arena; and NTT tables are computed at compile time. On the development
Intel Core i7-13700H, TMVP measured about 3.5 microseconds per multiplication
and the AVX2 schoolbook backend about 7.6 microseconds. Results depend on the
compiler, CPU, frequency policy, and thermal state.

## External backends still requiring configuration

The following canonical features need concrete external choices before they
can be implemented responsibly:

- the RV32 compiler triple, ABI, multilib, linker script, and allowed flags;
- the simulator and its machine-readable cycle/instruction-count interface;
- the real board or FPGA, deployment transport, firmware harness, cycle
  counter, and reset protocol;
- the CBMC version, loop-unwind policy, proof bounds, timeout budget, and exact
  properties required for release gating;
- the PQC integration target and revision, such as mlkem-native, liboqs, or a
  specific firmware tree, plus its required kernel ABI;
- experiment-budget policy for large recursive/hybrid search spaces.

These are isolated behind future backend boundaries. The current host path,
IR, plan checking, artifact generation, and reporting do not fabricate their
results.

## Portability limits

- The host selector uses exact unsigned 128-bit model arithmetic and therefore
  requires a GCC or Clang target providing `unsigned __int128`.
- Standalone emitted kernels do not require 128-bit arithmetic and can be
  cross-compiled for narrower targets such as RV32.
- Scratch accounting covers explicit algorithm arrays, not compiler spills or
  caller-owned buffers.
- Host code size falls back to optimized object-file size if no compatible
  section-size tool is available; provenance records that measurement path.
