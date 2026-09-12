# ML KEM custom instructions on PicoRV32

This project measures whether three small custom RISC-V instructions can accelerate complete ML-KEM on PicoRV32, and compares the cycle savings against the resulting FPGA area and timing cost.

The experiment has four builds

| Build | Only intended change from baseline |
| --- | --- |
| Baseline | no custom cryptographic instruction |
| FQMUL | replace coefficient multiply followed by Montgomery reduction with FQMUL |
| RED32 | keep ordinary RV32M multiplication and replace its software Montgomery reduction with RED32 |
| FSRI | replace the software expansion of each Keccak rotate with direct funnel shifts |

All four builds use [mlkem-native](https://github.com/pq-code-package/mlkem-native) at commit `69d24e37b8a04c6050ec55bc84a4228d7051bb4b` and [PicoRV32](https://github.com/YosysHQ/picorv32) at commit `a473fc8fca393771d83b0ffcf0b14db3393339d8`.

## Fair comparison

The pinned ML-KEM source does not expose a primitive hook at exactly the required boundary. [`fixed_backend.c`](targets/picorv32/mlkem/fixed_backend.c) therefore reproduces its fixed polynomial schedule. Baseline FQMUL and RED32 all compile that same file. The loops transform order reduction points constants and compiler flags are unchanged between them.

The primitive selected at compile time is the only arithmetic difference

```text
baseline   product = a * b       result = software Montgomery reduction
FQMUL                               result = FQMUL a b
RED32      product = RV32M MUL   result = RED32 product
```

FSRI leaves the same arithmetic backend in place. Its build uses the pinned Keccak source with only the rotate macro replaced. One 64 bit rotate becomes two direct FSRI instructions on RV32.

Every processor uses the same PicoRV32 parameters and the same multiplier in [`pqc_pcpi_mlkem.sv`](targets/picorv32/rtl/pqc_pcpi_mlkem.sv) for ordinary RV32M multiplication. Each custom build enables one additional decoder and data path. This avoids changing the normal multiplication architecture between the baseline and instruction builds.

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

The instruction wrappers and software references are in [`targets/picorv32/mlkem`](targets/picorv32/mlkem). The complete PCPI implementation is in [`pqc_pcpi_mlkem.sv`](targets/picorv32/rtl/pqc_pcpi_mlkem.sv). Historical sliced and multiplier reuse FSRI implementations are not part of this experiment.

## Complete ML KEM benchmark

[`mlkem_bench.c`](targets/picorv32/firmware/mlkem_bench.c) measures key generation encapsulation and decapsulation for ML-KEM-512 ML-KEM-768 and ML-KEM-1024. Each operation uses 30 deterministic inputs and three repeats. The firmware checks API success deterministic repeat outputs matching encapsulated and decapsulated shared secrets and rejection behavior after corrupting a ciphertext.

The simulator subtracts the measured MMIO marker overhead and records median PicoRV32 cycle counts. It also scans each disassembly to ensure only the intended custom encoding appears. Output checksums must match across all four variants for each parameter set before a complete summary can be produced.

### Cycle results

The previous checked in numbers compared different software schedules and are not valid for this narrower experiment. They were removed rather than relabeled. The table will be populated by the fair rerun.

| Parameter set | Baseline | FQMUL | RED32 | FSRI |
| --- | ---: | ---: | ---: | ---: |
| ML-KEM-512 | pending rerun | pending rerun | pending rerun | pending rerun |
| ML-KEM-768 | pending rerun | pending rerun | pending rerun | pending rerun |
| ML-KEM-1024 | pending rerun | pending rerun | pending rerun | pending rerun |

## Complete core synthesis

Yosys lowers the complete PicoRV32 core and selected PCPI hardware to ECP5 cells. nextpnr places and routes the same netlist with seeds 1 through 5. ecppack confirms that each routed configuration can be packed. Results record LUT4 flip flops DSP blocks BRAM blocks every routed maximum frequency and the median maximum frequency. The route requests 50 MHz and records whether each seed meets it.

Source netlist repository and tool hashes are kept with the raw synthesis results so a number can be tied to the exact input that produced it.

### Hardware results

These values also require a new run because the processor multiplier and enabled hardware matrix changed with the fair baseline.

| Complete core | LUT4 | FF | DSP | BRAM | Median Fmax |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| FQMUL | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| RED32 | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |
| FSRI | pending rerun | pending rerun | pending rerun | pending rerun | pending rerun |

The canonical machine readable file is [`results/summary.json`](results/summary.json). It says `pending fair rerun` until all 12 ML-KEM measurements and all four five-seed synthesis results pass the completeness checks.

## Build and reproduce

Run every command below from the repository root.

### Software build and tests

The host tests need CMake a C and C++ compiler and Python. The first build matches the optimized CI configuration. The second runs the same tests with address and undefined behavior sanitizers.

```sh
cmake -S . -B build/release -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DPQC_POLY_LTO=ON
cmake --build build/release --parallel 4
ctest --test-dir build/release --output-on-failure

cmake -S . -B build/sanitize -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug -DPQC_POLY_LTO=OFF -DPQC_POLY_SANITIZE=ON
cmake --build build/sanitize --parallel 4
ctest --test-dir build/sanitize --output-on-failure
```

### Direct PCPI RTL tests

PicoRV32 targets use the pinned RISC-V GNU toolchain release `2026.07.15` and OSS CAD Suite release `2026-07-29`. Put the required executables on `PATH`. The direct instruction tests only need Verilator and Ninja from the CAD suite.

```sh
cmake -S . -B build/picorv32-sim -G Ninja -DCMAKE_BUILD_TYPE=Release -DPQC_POLY_LTO=OFF -DPQC_POLY_PICORV32=ON
cmake --build build/picorv32-sim --target pqc-picorv32-sim --parallel 4
```

This builds four direct PCPI models and checks disabled decoding plus the FQMUL RED32 and FSRI implementations against their software references.

### Complete experiment

Configure one build with the complete ML-KEM formal and synthesis paths enabled.

```sh
cmake -S . -B build/picorv32-experiment -G Ninja -DCMAKE_BUILD_TYPE=Release -DPQC_POLY_LTO=OFF -DPQC_POLY_PICORV32_MLKEM=ON -DPQC_POLY_PICORV32_FORMAL=ON -DPQC_POLY_PICORV32_SYNTHESIS=ON
```

Run all 12 complete ML-KEM simulations.

```sh
cmake --build build/picorv32-experiment --target pqc-picorv32-mlkem --parallel 4
```

Run the three bounded instruction checks.

```sh
cmake --build build/picorv32-experiment --target pqc-picorv32-formal --parallel 4
```

Synthesize place and route all four complete processor variants.

```sh
cmake --build build/picorv32-experiment --target pqc-picorv32-synthesis --parallel 4
```

Generate the checked summary from the complete measurement matrix then generate the local SVG figures. The SVG files are build artifacts and are not committed.

```sh
cmake --build build/picorv32-experiment --target pqc-picorv32-results --parallel 4
python3 scripts/results.py --input build/picorv32-experiment/targets/picorv32/results --output results/summary.json
python3 scripts/readme_figures.py
```

The main targets are

| Target | Purpose |
| --- | --- |
| `pqc-picorv32-sim` | direct PCPI reference tests for all instruction settings |
| `pqc-picorv32-baseline` | three complete baseline ML-KEM measurements |
| `pqc-picorv32-fqmul` | three complete FQMUL measurements |
| `pqc-picorv32-red32` | three complete RED32 measurements |
| `pqc-picorv32-fsri` | three complete direct FSRI measurements |
| `pqc-picorv32-mlkem` | all 12 complete ML-KEM measurements |
| `pqc-picorv32-formal` | three direct bounded PCPI checks |
| `pqc-picorv32-synthesis` | four complete core ECP5 results |
| `pqc-picorv32-results` | require the complete measurement matrix and create the summary |

## Checks kept in scope

- C and C++ reference tests cover FQMUL RED32 FSRI and the fixed backend for all three vector widths
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
| [`targets/picorv32/synth`](targets/picorv32/synth) | complete core Yosys and ECP5 routing flow |
| [`scripts`](scripts) | result validation and README figure generation |

## Standards and tools

ML-KEM is specified in [FIPS 203](https://csrc.nist.gov/pubs/fips/203/final). SHA3 and SHAKE are specified in [FIPS 202](https://csrc.nist.gov/pubs/fips/202/final). The implementation flow uses Verilator for RTL simulation Yosys for synthesis and nextpnr for ECP5 placement and routing.
