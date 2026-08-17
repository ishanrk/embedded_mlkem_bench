# picoRV32 step 1 devlog

## start

- date: 2026-08-16
- head: `0fdc1dae3f8d02e9d8d3c8f8683ce871bd96f4fa`
- worktree: clean
- repository instructions: none found inside `pqc-poly-bench`
- scope: reproducible picoRV32 rv32imc execution measurement and synthesis only
- excluded: ml-kem schedules and `mlk.fqmul`

## decisions

- preserve the existing host selector formula kernels tests and cli
- keep `limits.ram` limited to generated kernel scratch
- make target configuration opt in so normal host presets never fetch picoRV32
- retain only real measurements under `results/raw`
- pin picoRV32 with the official codeload archive hash
- keep target compilation as explicit custom commands so host compilers remain unchanged
- use external marker handshake cycles as the only target cycle source
- return unknown for unproved recursive indirect or missing callchain frames
- keep project and stock multiplier simulation outputs separate

## commands

- `git status --short`
- `git log -5 --oneline`
- repository file inventory with `rg --files`
- inspected `CMakeLists.txt` `CMakePresets.json` `.gitignore` and `README.md`
- downloaded the pinned picoRV32 source archive for read only interface inspection
- verified archive sha256 `050ba03d03eaacadb5953f3ba2218b49866c71d505c2476e49a0c0f5fe14e36c`
- checked target tool availability and versions
- configured a target CMake graph with explicit no op tool paths and the verified local source
- inspected the generated target command graph with `ninja -t commands`
- ran host C syntax checks for firmware sources
- ran strict host C++ syntax checks for both simulator modes with temporary interface stubs
- ran a Yosys parse hierarchy process and check pass over the core and project PCPI RTL
- ran `git diff --check`
- ran clang format verification over every new C++ file
- validated Python syntax for `ecp5-50mhz.py`
- configured simulation and synthesis command graphs with explicit diagnostic tool substitutes
- ran final release and sanitizer builds and tests after all edits

## tests and measurements

- baseline release: 8 of 8 passed
- baseline sanitizer: 8 of 8 passed with `ASAN_OPTIONS=detect_leaks=0`
- implemented release: 10 of 10 passed
- implemented sanitizer: 10 of 10 passed with `ASAN_OPTIONS=detect_leaks=0`
- firmware host syntax check: passed
- simulator host syntax checks: passed
- RTL structural Yosys check: passed with available Yosys 0.9
- Clang release: new target measurement and manifest tests passed
- Clang release: 7 of 10 overall passed
- target measurements: not obtained
- `git diff --check`: passed
- final head remains `0fdc1dae3f8d02e9d8d3c8f8683ce871bd96f4fa`
- final release rerun: 10 of 10 passed
- final sanitizer rerun: 10 of 10 passed with `ASAN_OPTIONS=detect_leaks=0`
- final clang format Python syntax preset JSON and diff checks: passed

## baseline commands

- `cmake --preset release`
- `cmake --build --preset release --parallel`
- `ctest --preset release`
- `cmake --preset sanitize`
- `cmake --build --preset sanitize --parallel`
- `ASAN_OPTIONS=detect_leaks=0 ctest --preset sanitize`

## initial skipped checks and blockers

- RISC-V GNU tools missing: `riscv32-unknown-elf-gcc` `ld` `objcopy` `objdump` `size`
- Verilator missing
- nextpnr-ecp5 and ecppack missing
- available Yosys 0.9 does not match OSS CAD Suite release `2026-07-29`
- available Z3 is 4.8.12 and does not establish provenance from the pinned CAD release
- no RV32 firmware build simulation PCPI execution stack canary run or cycle comparison possible
- no five seed ECP5 synthesis possible
- no result copied to `results/raw` because incomplete or synthetic results are forbidden
- extra Clang suite failed existing codegen host tests because generated vectorization hints trigger `-Wpass-failed` under Clang 14
- no Step 1 file changes the existing generated kernel vectorization hint

## pinned local tools

- RISC-V release `2026.07.15`
- archive `riscv32-elf-ubuntu-22.04-gcc.tar.xz`
- sha256 `ae36abbec394b29643154c1b4a1322e829937d04e82f41b47f9c27d3bd68e543`
- install `/home/ishan/.local/toolchains/riscv32-elf-2026.07.15`
- bin `/home/ishan/.local/toolchains/riscv32-elf-2026.07.15/bin`
- gcc `riscv32-unknown-elf-gcc (g6afcc4f6d) 16.1.0`
- binutils `GNU Binutils 2.46`
- OSS CAD Suite release `2026-07-29`
- archive `oss-cad-suite-linux-x64-20260729.tgz`
- sha256 `89ea1152ea84bc600f18cc685f721d534d1f018e09831662787865a3d79ce4aa`
- install `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29`
- bin `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29/bin`
- Verilator `5.051 devel rev v5.050-108-gcac2c3df1 (mod)`
- Yosys `0.67+111 (git sha1 4821ed17b-dirty, Release, Clang /usr/bin/clang++ 21.1.8)`
- nextpnr `nextpnr-0.10-106-g00376b6b`
- Project Trellis ecppack `1.4-79-g56bb170`
- SymbiYosys `v0.67-4-gfea6e46`
- Z3 `4.15.5`

## executable paths

- `/home/ishan/.local/toolchains/riscv32-elf-2026.07.15/bin/riscv32-unknown-elf-gcc`
- `/home/ishan/.local/toolchains/riscv32-elf-2026.07.15/bin/riscv32-unknown-elf-ld`
- `/home/ishan/.local/toolchains/riscv32-elf-2026.07.15/bin/riscv32-unknown-elf-objcopy`
- `/home/ishan/.local/toolchains/riscv32-elf-2026.07.15/bin/riscv32-unknown-elf-objdump`
- `/home/ishan/.local/toolchains/riscv32-elf-2026.07.15/bin/riscv32-unknown-elf-size`
- `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29/bin/verilator`
- `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29/bin/yosys`
- `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29/bin/nextpnr-ecp5`
- `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29/bin/ecppack`
- `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29/bin/sby`
- `/home/ishan/.local/toolchains/oss-cad-suite-2026-07-29/bin/z3`

## target continuation

- installed both verified archives without root or global shell changes
- added `.local/picorv32-tools.sh` for the pinned local path
- fixed trap firmware unused helpers and replaced the invalid C misalignment probe with RV32 assembly
- made the bare metal link explicitly non PIE and placed linker relocation metadata
- invoked the Yosys synthesis source as Tcl because it uses Tcl environment variables
- used pinned nextpnr `--lpf-allow-unconstrained` because this release rejects `--no-iobs`
- verified the counted Yosys top contains zero `TRELLIS_IO` cells
- corrected maximum frequency to the final routed report
- corrected LUT4 accounting to include logic carry and distributed RAM LUTs
- added allocated non NOBITS flash accounting
- recorded the tool archives in the generated target manifest
- release tests 10 of 10 passed
- sanitizer tests 10 of 10 passed with `ASAN_OPTIONS=detect_leaks=0`
- PCPI unit test passed 10000 seeded random requests plus boundary and control cases
- project and stock multiplier runs each measured 3249 calibrated cycles
- project versus stock difference 0 percent
- explicit scratch 0 bytes
- caller working storage 4 bytes
- compiler frame 32 bytes
- compiler callchain bound 32 bytes
- runtime stack high water 32 bytes
- text 1260 bytes
- rodata 28 bytes
- data 0 bytes
- bss 16 bytes
- allocated flash 1288 bytes
- all five seeds use 3583 LUT4 970 flip flops 4 DSP and 0 BRAM
- seed frequencies 69.90 70.68 68.70 66.39 and 66.99 MHz
- every seed passed 50 MHz and ecppack
- raw baseline text contains zero custom opcode matches
- final ELF has no unresolved symbols build ID or unexpected non RV32IMC attribute
- fetched PicoRV32 source matches the verified local source tree
- finalized seven real artifacts under `results/raw/picorv32-step1-0fdc1dae-a473fc8f`
- no Step 2 or Step 3 code was added
- no commit push remote change or pull request was made
