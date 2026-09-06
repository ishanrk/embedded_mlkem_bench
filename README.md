# ML KEM custom instructions on PicoRV32

This project measures whether four small custom RISC-V instructions can accelerate complete ML-KEM on PicoRV32, and compares the cycle savings against the resulting FPGA area and timing cost.

The experiment has five builds

| Build | Only intended change from baseline |
| --- | --- |
| Baseline | no custom cryptographic instruction |
| FQMUL | replace coefficient multiply followed by Montgomery reduction with FQMUL |
| RED32 | keep ordinary RV32M multiplication and replace its software Montgomery reduction with RED32 |
| FSRI | replace the software expansion of each Keccak rotate with direct funnel shifts |
| DOT2X | replace each pair of products in cached base multiplication with DOT2X |

All five builds use [mlkem-native](https://github.com/pq-code-package/mlkem-native) at commit `69d24e37b8a04c6050ec55bc84a4228d7051bb4b` and [PicoRV32](https://github.com/YosysHQ/picorv32) at commit `a473fc8fca393771d83b0ffcf0b14db3393339d8`.

## Fair comparison

The pinned ML-KEM source does not expose a primitive hook at exactly the required boundary. [`fixed_backend.c`](targets/picorv32/mlkem/fixed_backend.c) therefore reproduces its fixed polynomial schedule. Baseline FQMUL RED32 and DOT2X all compile that same file. The loops transform order reduction points constants and compiler flags are unchanged between them.

The primitive selected at compile time is the only arithmetic difference

```text
baseline   product = a * b       result = software Montgomery reduction
FQMUL                               result = FQMUL a b
RED32      product = RV32M MUL   result = RED32 product
DOT2X      product = DOT2X on two packed coefficient pairs
```

FSRI leaves the same arithmetic backend in place. Its build uses the pinned Keccak source with only the rotate macro replaced. One 64 bit rotate becomes two direct FSRI instructions on RV32.

Every processor uses the same PicoRV32 parameters and the same multiplier in [`pqc_pcpi_mlkem.sv`](targets/picorv32/rtl/pqc_pcpi_mlkem.sv) for ordinary RV32M multiplication. DOT2X uses that multiplier for both products over two cycles. Each custom build enables one additional decoder and data path. This avoids changing the normal multiplication architecture between the baseline and instruction builds.

## Instructions

FQMUL multiplies the signed low halves of its two source registers and Montgomery reduces the product modulo 3329. It responds after four PCPI cycles.

```text
product = signed16(rs1) * signed16(rs2)
inverse = signed16(low16(product * 62209))
rd      = (product - inverse * 3329) / 65536
```

RED32 receives a signed product in its first source register and performs only the same Montgomery reduction. The normal RISC-V `MUL` remains a separate instruction. RED32 responds after three PCPI cycles.

FSRI directly returns the low word of two joined registers shifted by its immediate. It has a combinational response path.

```text
rd = low32(({rs2, rs1} >> shamt)
```

DOT2X treats each source register as two signed 16 bit coefficients and computes
`a0 * b1 + a1 * b0`. It targets the two product expressions in cached ML-KEM
base multiplication while preserving accumulation before Montgomery reduction.
It responds after three PCPI cycles and uses opcode `0x0b` with `funct3 = 3`
and `funct7 = 0`.

DOT2X tests whether packed two product arithmetic provides a better complete
system area versus cycle tradeoff than the existing smaller primitives. The
operation itself is not presented as novel. The contribution is its mapping to
this ML-KEM backend through PicoRV32 PCPI with multiplier reuse simulation
checks formal checking complete ML-KEM benchmarking and area and timing
evaluation.

The instruction wrappers and software references are in [`targets/picorv32/mlkem`](targets/picorv32/mlkem). The complete PCPI implementation is in [`pqc_pcpi_mlkem.sv`](targets/picorv32/rtl/pqc_pcpi_mlkem.sv). Historical sliced and multiplier reuse FSRI implementations are not part of this experiment.

## Complete ML KEM benchmark

[`mlkem_bench.c`](targets/picorv32/firmware/mlkem_bench.c) measures key generation encapsulation and decapsulation for ML-KEM-512 ML-KEM-768 and ML-KEM-1024. Each operation uses 30 deterministic inputs and three repeats. The firmware checks API success deterministic repeat outputs matching encapsulated and decapsulated shared secrets and rejection behavior after corrupting a ciphertext.

The simulator subtracts the measured MMIO marker overhead and records median PicoRV32 cycle counts. It also scans each disassembly to ensure only the intended custom encoding appears. Output checksums must match across all five variants for each parameter set before a complete summary can be produced.

### Performance

The generated figures read individual verified values from the canonical summary. Missing measurements stay visibly pending until the fair rerun fills them.

#### Total cycles

![Complete ML-KEM cycle count for Baseline FQMUL RED32 FSRI and DOT2X](docs/figures/total-cycles.svg)

#### Operation breakdown

![ML-KEM operation cycle counts for Baseline FQMUL RED32 FSRI and DOT2X](docs/figures/operation-cycles.svg)

#### Exact cycle results

The previous checked in numbers compared different software schedules and are not valid for this narrower experiment. They were removed rather than relabeled. The table will be populated by the fair rerun.

| Parameter set | Baseline | FQMUL | RED32 | FSRI | DOT2X |
| --- | ---: | ---: | ---: | ---: | ---: |
| ML-KEM-512 | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| ML-KEM-768 | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| ML-KEM-1024 | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |

## Complete core synthesis

Yosys lowers the complete PicoRV32 core and selected PCPI hardware to ECP5 cells. nextpnr places and routes the same netlist with seeds 1 through 5. ecppack confirms that each routed configuration can be packed. Results record LUT4 flip flops DSP blocks BRAM blocks every routed maximum frequency and the median maximum frequency. The route requests 50 MHz and records whether each seed meets it.

Source netlist repository and tool hashes are kept with the raw synthesis results so a number can be tied to the exact input that produced it.

### Hardware cost

#### LUT4 area

![Complete core LUT4 counts for Baseline FQMUL RED32 FSRI and DOT2X](docs/figures/lut4-area.svg)

#### Flip flops

![Complete core flip-flop counts for Baseline FQMUL RED32 FSRI and DOT2X](docs/figures/flip-flops.svg)

#### Routed maximum frequency

![Median routed maximum frequency and seed results for Baseline FQMUL RED32 FSRI and DOT2X](docs/figures/fmax.svg)

### Hardware results

These values also require a new run because the processor multiplier and enabled hardware matrix changed with the fair baseline.

| Complete core | LUT4 | FF | DSP | BRAM | Median Fmax |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| FQMUL | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| RED32 | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| FSRI | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| DOT2X | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |

The canonical machine readable file is [`results/summary.json`](results/summary.json). It says `pending fair rerun` until all 15 ML-KEM measurements and all five five-seed synthesis results pass the completeness checks.

## Build and reproduce

Run every command below from the repository root.

The responsibilities are deliberately split along the normal build-versus-run
boundary. CMake compiles host tests, Verilator models, and RISC-V firmware while
[`run_experiment.py`](scripts/run_experiment.py) runs the benchmark matrix,
formal checks, synthesis, and result aggregation.

### Software build and tests

The host tests need CMake, C and C++ compilers, and Python. The second build runs
the same tests with address and undefined behavior sanitizers.

```sh
cmake -S . -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure

cmake -S . -B build/sanitize -DCMAKE_BUILD_TYPE=Debug -DPQC_POLY_SANITIZE=ON
cmake --build build/sanitize --parallel 4
ctest --test-dir build/sanitize --output-on-failure
```

### PicoRV32 build products

PicoRV32 build products use RISC-V GNU toolchain release `2026.07.15` and OSS
CAD Suite release `2026-07-29`. Put `verilator`,
`riscv32-unknown-elf-gcc`, `riscv32-unknown-elf-objcopy`, and
`riscv32-unknown-elf-objdump` on `PATH`. CMake does not require the formal or
synthesis tools.

```sh
cmake -S . -B build/picorv32 -DCMAKE_BUILD_TYPE=Release -DPQC_POLY_PICORV32=ON
cmake --build build/picorv32 --target pqc-picorv32-cpu-models pqc-picorv32-firmware --parallel 4
```

The firmware target creates ELF, HEX, and disassembly files for all 15
variant/parameter-set combinations. Individual targets follow the form
`pqc-picorv32-firmware-fqmul-768`. `pqc-picorv32-sim` remains a small direct RTL
correctness target that builds and runs the five PCPI reference tests.

### Complete experiment

The Python runner configures CMake when build products are needed. Each stage can
be run independently:

```sh
python3 scripts/run_experiment.py --pcpi
python3 scripts/run_experiment.py --bench
python3 scripts/run_experiment.py --formal
python3 scripts/run_experiment.py --synthesis
```

Run the complete flow, including final summary generation, with:

```sh
python3 scripts/run_experiment.py --all
```

The default build directory is `build/picorv32`; use `--build-dir` to change it.
Runtime tool paths can be selected with `--sby`, `--yosys`, `--nextpnr`, and
`--ecppack`. Extra CMake cache settings can be passed with repeated
`--cmake-arg` options.

The runner keeps the same 30 deterministic inputs, simulator arguments,
disassembly validation, formal jobs, synthesis seeds, and JSON filenames. To
update the checked summary and local figures after a complete run:

```sh
python3 scripts/results.py --input build/picorv32/targets/picorv32/results --output results/summary.json
python3 scripts/readme_figures.py
```

The main CMake targets are build products rather than experiment stages:

| CMake target | Product |
| --- | --- |
| `pqc-picorv32-sim` | direct PCPI reference tests for all instruction settings |
| `pqc-picorv32-pcpi-models` | five direct PCPI Verilator executables |
| `pqc-picorv32-cpu-models` | five complete PicoRV32 Verilator executables |
| `pqc-picorv32-firmware` | all 15 RISC-V firmware ELF, HEX, and disassembly sets |
| `pqc-picorv32-firmware-<variant>-<level>` | one firmware artifact set |

## Checks kept in scope

- C and C++ reference tests cover FQMUL RED32 FSRI DOT2X and the fixed backend for all three vector widths
- Verilator compares each instruction RTL against its reference including boundaries random operands reset latency decoding and consecutive requests
- whole processor simulation boots bare metal firmware and runs complete ML-KEM
- each ML-KEM build checks complete cryptographic operation results deterministic repeats and intended instruction use
- short bounded SymbiYosys jobs compare each PCPI response with its arithmetic reference for arbitrary operands in the explored trace
- Yosys nextpnr and ecppack measure the complete core rather than the isolated instruction block

The formal checks are in [`targets/picorv32/formal`](targets/picorv32/formal). They are deliberately local and bounded. They do not prove ML-KEM or PicoRV32 as a whole.

## Limitations

- cycle counts come from PicoRV32 RTL simulation under Verilator and are not wall clock measurements
- hardware values come from ECP5 synthesis placement and routing rather than a physical board measurement
- the project makes no claim of physical side channel resistance
- formal checking covers local custom instruction properties within bounded traces not complete ML-KEM or the whole processor
- the checked in result table remains pending until the new fair matrix is rerun with the pinned heavy toolchains

## Repository map

| Path | Contents |
| --- | --- |
| [`targets/picorv32/mlkem`](targets/picorv32/mlkem) | fixed arithmetic backend and instruction wrappers |
| [`targets/picorv32/rtl`](targets/picorv32/rtl) | PCPI instruction block and complete core wrappers |
| [`targets/picorv32/firmware`](targets/picorv32/firmware) | bare metal startup runtime and complete benchmark |
| [`targets/picorv32/sim`](targets/picorv32/sim) | direct instruction and complete processor Verilator drivers |
| [`targets/picorv32/formal`](targets/picorv32/formal) | direct bounded PCPI properties |
| [`targets/picorv32/synth`](targets/picorv32/synth) | complete core Yosys script |
| [`scripts`](scripts) | experiment orchestration, ECP5 routing, result validation, and README figures |

## Standards and tools

ML-KEM is specified in [FIPS 203](https://csrc.nist.gov/pubs/fips/203/final). SHA3 and SHAKE are specified in [FIPS 202](https://csrc.nist.gov/pubs/fips/202/final). The implementation flow uses Verilator for RTL simulation Yosys for synthesis and nextpnr for ECP5 placement and routing.
