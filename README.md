# pqc-poly-bench

`pqc-poly-bench` is a reproducible research harness for ML-KEM software scheduling and
small RISC-V ISA extensions on a resource-constrained PicoRV32 target. It enumerates a
fixed ML-KEM schedule space, independently checks arithmetic and memory invariants,
generates C backends, measures every legal schedule end to end, and compares software
winners against hardware/software co-design variants.

The repository is intentionally narrow. Earlier generic schoolbook polynomial-selection
and fixed NTRU formula experiments have been removed from the active codebase because
they are not part of the ML-KEM Step 1-3 results.

## Build and test

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
```

For ASan and UBSan:

```bash
cmake --preset sanitize
cmake --build --preset sanitize --parallel
ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize
```

Normal host builds are offline. PicoRV32 and `mlkem-native` are fetched only by the
explicit opt-in target flows.

## ML-KEM schedule space

`pqc-poly-mlkem` enumerates 144 stable plans over ML-KEM-512, ML-KEM-768, and
ML-KEM-1024. Each level has 24 software plans and the same 24 plans using the Step 3
`mlk.fqmul` instruction. The search dimensions are:

- forward NTT traversal: stage-major or two-layer fusion;
- inverse NTT traversal: stage-major or two-layer fusion;
- inverse sum reduction: every layer or after each layer pair;
- base multiplication: cached late reduction, cached eager reduction, or direct eager
  reduction;
- instruction set: baseline or `mlk.fqmul`.

The input spec contains only the generated-kernel scratch limit:

```json
{
  "scratch_limit": 4096
}
```

The ML-KEM ring, modulus, coefficient representation, and parameter dimensions are fixed
by the experiment rather than repeated as generic polynomial-selector fields. Caller
working storage remains a separate measured quantity and is not folded into
`scratch_limit`.

Generate all candidates and legal backends with:

```bash
./build/release/pqc-poly-mlkem examples/mlkem.json -o mlkem-out
```

The independent checker validates plan identity, butterfly and twiddle schedules,
fusion groupings, interval bounds, Montgomery and Barrett preconditions, explicit
scratch, caller workspace, and the declared rejection set before code generation.

## PicoRV32 Step 1

The target flow pins PicoRV32 at
`a473fc8fca393771d83b0ffcf0b14db3393339d8`. It builds bare-metal RV32IMC smoke
firmware, supplies a project PCPI implementation of the four standard multiply
instructions, and compares it with PicoRV32's stock fast multiplier. PicoRV32 itself
remains unmodified and supplies division.

```bash
cmake --preset picorv32-sim
cmake --build --preset picorv32-sim

cmake --preset picorv32-synthesis
cmake --build --preset picorv32-synthesis
cmake --build build/picorv32-synthesis --target pqc-picorv32-finalize
```

The completed Step 1 run measured 3249 calibrated cycles for both project and stock
multipliers, 32 bytes of runtime stack high water, and 1288 allocated flash bytes. Five
ECP5 seeds used 3583 LUT4, 970 flip-flops, 4 DSP blocks, and no block RAM; routed
frequencies ranged from 66.39 to 70.68 MHz. These are substrate measurements, not
ML-KEM performance results.

Historical Step 1 records remain under `results/raw/` with their original commit IDs.
They are retained as provenance rather than rewritten by this cleanup.

## Step 2: software schedule search

The ML-KEM flow pins `mlkem-native` at
`69d24e37b8a04c6050ec55bc84a4228d7051bb4b` with archive SHA-256
`5f83af0a01fbed2c2d6cc370b56909f3b062728cff0ec9f310314707f13a1f3e`.
It measures the upstream portable implementation and every software schedule with 16
fixed kernel inputs and 30 fixed complete-operation inputs. Every input runs three times;
repeat drift fails the run.

```bash
cmake -S . -B build/picorv32-mlkem -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DPQC_POLY_LTO=OFF \
  -DPQC_POLY_PICORV32_MLKEM=ON
cmake --build build/picorv32-mlkem --target pqc-picorv32-mlkem --parallel
cmake --build build/picorv32-mlkem --target pqc-picorv32-mlkem-finalize
```

All three parameter sets selected the software schedule
`ffuse2_ifuse2_rpair_bcachelate_xnone`. The completed records are retained under
`results/raw/picorv32-step2-3f13dce5-69d24e37/`.

## Step 3: `mlk.fqmul`

Step 3 adds `mlk.fqmul`, a four-cycle signed-low-half Montgomery multiplication in the
existing project PCPI multiplier. The full 72-plan custom-instruction space was measured
with the same input and repeat protocol as Step 2.

| level | jointly selected plan | complete cycles | versus software |
|---|---|---:|---:|
| 512 | `mlk512_ffuse2_ifuse2_rpair_bcacheeager_xfqmul` | 11,703,506 | 7.39% faster |
| 768 | `mlk768_ffuse2_ifuse2_rpair_bdirecteager_xfqmul` | 19,023,552 | 5.56% faster |
| 1024 | `mlk1024_ffuse2_ifuse2_rpair_bcacheeager_xfqmul` | 29,318,051 | 5.32% faster |

The nine operation-level comparisons have geometric-mean speedup 1.063700672. Joint
schedule selection improves on staged `fqmul` by geometric mean 1.005931394, below the
declared 3% co-tuning threshold, although the fastest schedule changes at every level.
The declared 10% material-improvement threshold also fails.

Five-seed ECP5 synthesis keeps 4 DSP blocks and no block RAM, and every seed remains
above 50 MHz. The hardware gates nevertheless reject the instruction: median LUT4 rises
from 3583 to 3788, median flip-flops from 970 to 1053, and median routed frequency falls
from 68.70 to 60.51 MHz. The project therefore does not recommend `fqmul` on this target.
Canonical Step 3 records are under
`results/raw/picorv32-step3-fd803594-69d24e37/`.

## Verification limits

A selectable plan must pass the independent ML-KEM plan checker and the measurement
completeness checks. Generated backends are differentially tested, including ASan and
UBSan host tests. Step 3 additionally checks the portable `fqmul` model with CBMC 6.10.0,
proves PCPI arithmetic and handshake properties with SymbiYosys, and performs a bounded
RVFI retirement check.

These checks do not prove complete ML-KEM, constant-time C execution, or physical
side-channel resistance. Reported target cycles come from the pinned RTL simulation,
not host timing or an instruction-set simulator.

## Repository layout

- `src/mlkem_plan.cpp`: stable schedule enumeration, bounds, scratch accounting, and
  measured-plan selection;
- `src/mlkem_check.cpp`: independent reconstruction and consistency checks;
- `src/mlkem_codegen.cpp`: C backend generation for software and `fqmul` plans;
- `src/mlkem_explore.cpp`: experiment artifact generation and Step 2/3 selection;
- `src/target_measurement.cpp`: target cycle, stack, ELF, manifest, and synthesis parsers;
- `targets/picorv32/`: firmware, RTL, simulation, formal checks, and ECP5 synthesis;
- `results/raw/`: retained completed experiment records;
- `tests/`: ML-KEM, target-measurement, generated-backend, and `fqmul` tests.

## Remaining research work

The next planned comparison is Step 4 `red32`, run through the same complete software
re-search, end-to-end measurement, verification, and hardware-gating pipeline. A
publication-quality comparison against prior RISC-V ML-KEM implementations also remains
to be completed.
