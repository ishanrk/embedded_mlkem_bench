# Project Idea

ML-KEM is a post-quantum key encapsulation algorithm. It is useful for allowing two parties to establish a shared secret, which can then be used to set up an encrypted communication channel.

ML-KEM spends a lot of its runtime doing two things on a processor.

a) The first is polynomial arithmetic such as the NTT [1], inverse NTT, field multiplication, and Montgomery reduction. Montgomery reduction is basically a way of avoiding expensive division operations when repeatedly reducing values modulo the field order $q=3329$.

b) The second is SHA-3/SHAKE [2]. These are used for things like generating the public matrix, hashing values, deriving randomness, and deriving the final shared secret. They use the Keccak permutation [3], which performs a large number of rotations on 64-bit values.

The goal of this project was to see whether adding small RISC-V hardware extensions [4], which can be thought of as more specialized instructions that a processor is allowed to execute, could make complete ML-KEM faster on an embedded processor without adding too much hardware area or delay.

I used PicoRV32 as the processor and `mlkem-native` [5] as the ML-KEM implementation. PicoRV32 is configured as RV32IMC here, so it already has normal hardware multiplication. This matters because otherwise an instruction such as FQMUL would look much better partly because I had added a multiplier to a processor that did not have one before.

Before adding any custom hardware I first tried different ways of arranging the main ML-KEM polynomial operations in software.

The idea is that the same arithmetic can sometimes be carried out with fewer instructions if you are careful about when reductions happen.

For example, suppose several operations eventually need to be reduced modulo $q$. You could reduce after every operation. This keeps the value small, but it means doing more reductions. You could instead keep the intermediate value in a larger register, do several operations, and reduce only once at the end. This saves instructions, but if you wait for too long the value can get large enough to overflow the register. So there is a limit to how far you can push this.

For each ML-KEM parameter set I tested 24 different software implementations. The things I changed were:

| Part                | What I changed                                   |
| ------------------- | ------------------------------------------------ |
| forward NTT         | one layer at a time or two layers fused together |
| inverse NTT         | one layer at a time or two layers fused together |
| inverse reductions  | reduce every layer or after each pair of layers  |
| base multiplication | cached late, cached eager, or direct eager       |

The same combination ended up being fastest for ML-KEM-512, ML-KEM-768, and ML-KEM-1024. Its internal name is `ffuse2_ifuse2_rpair_bcachelate`.

Just rearranging the software already saves around 2% of the complete ML-KEM cycles. So when I test the hardware instructions I compare them against this version rather than giving the hardware credit for something that could already have been done in software.

I then implemented three custom RISC-V instructions that I thought could help.

1. `mlk.fqmul` combines coefficient multiplication and Montgomery reduction. This mainly speeds up the polynomial arithmetic used in the NTT and inverse NTT.

2. `mlk.red32` performs Montgomery reduction on an already-computed 32-bit product. The multiplication still uses the normal RISC-V `MUL` instruction.

3. `fsri` is a funnel-shift-right-immediate instruction. Keccak uses 64-bit rotations, but PicoRV32 only has 32-bit registers. FSRI makes these rotations much cheaper and therefore speeds up the SHA-3/SHAKE part of ML-KEM.

I then run complete key generation, encapsulation, and decapsulation using each version and compare two things. The first is how many processor cycles are saved. The second is how much extra hardware and delay the instruction adds.

# Instruction Design and Source Code

All three instructions are connected to PicoRV32 through its Pico Co-Processor Interface, or PCPI.

PCPI lets PicoRV32 give an instruction and its source registers to an external hardware unit. The hardware performs the operation and returns the result to the processor.

The main custom instruction logic is in [`targets/picorv32/rtl/pqc_pcpi_mlkem.sv`](targets/picorv32/rtl/pqc_pcpi_mlkem.sv). The PicoRV32 integration is in [`targets/picorv32/rtl/pqc_picorv32_core_top.sv`](targets/picorv32/rtl/pqc_picorv32_core_top.sv).

PicoRV32 itself is written in Verilog. The custom instruction hardware I added is written in SystemVerilog. The C implementations and inline instruction wrappers are under [`targets/picorv32/mlkem/`](targets/picorv32/mlkem/), and the complete ML-KEM benchmark is in [`targets/picorv32/firmware/mlkem_bench.c`](targets/picorv32/firmware/mlkem_bench.c).

## 1. FQMUL

ML-KEM repeatedly multiplies two coefficients and then Montgomery-reduces the result modulo 3329.

FQMUL combines these into one instruction.

```text
a = sign16(rs1)
b = sign16(rs2)
t = a * b
u = sign16((low16(t) * 62209) mod 2^16)
rd = (t - u * 3329) / 2^16
```

The low 16 bits of `rs1` and `rs2` hold the two coefficients. The instruction multiplies them, performs the same Montgomery reduction used by ML-KEM, and returns the result in `rd`.

This is useful throughout the NTT, inverse NTT, and other polynomial arithmetic where the same multiply-and-reduce operation occurs repeatedly.

FQMUL takes four PCPI cycles.

It also does not get a completely separate multiplier. It reuses the multiplier hardware already needed for normal RV32M multiplication. Otherwise a large part of the hardware cost would just be the cost of adding another multiplier rather than the ML-KEM-specific logic.

## 2. RED32

RED32 tests a smaller version of the same idea.

Instead of putting multiplication and reduction into one instruction, the processor first performs a normal RISC-V `MUL`. RED32 then takes that 32-bit result and performs only the Montgomery reduction.

```text
t = sign32(rs1)
u = sign16((low16(t) * 62209) mod 2^16)
rd = (t - u * 3329) / 2^16
```

So a normal field multiplication becomes roughly:

```text
MUL
RED32
```

RED32 takes three PCPI cycles.

I included it for two reasons. First, some parts of the polynomial arithmetic can keep ordinary multiplication in the existing RISC-V path and only accelerate the reduction. Second, standalone Montgomery-reduction instructions have already been studied for lattice cryptography [6], so this gives a useful comparison with that direction.

## 3. FSRI

FSRI targets a completely different part of ML-KEM.

SHA-3 and SHAKE use Keccak-f[1600]. Keccak works on 64-bit words and performs many fixed rotations on them.

PicoRV32 is a 32-bit processor. So one 64-bit value has to be stored using two 32-bit registers, and a 64-bit rotation normally turns into several shifts and OR operations.

FSRI performs a funnel shift:

```text
fsri rd, rs1, rs2, shamt
rd = low32(({rs2, rs1} >> shamt))
```

The instruction treats `rs1` and `rs2` as two pieces of a larger value and directly returns the shifted 32-bit piece. Swapping the two source registers gives the other half needed for the 64-bit rotation, so one Keccak rotation uses two FSRIs.

The idea of a funnel-shift instruction itself is not new. FSRI is a small project-specific version of an earlier RISC-V Bitmanip funnel-shift proposal [7]. What I wanted to measure here was whether it was actually useful for complete ML-KEM on a small RV32 processor.

I tried two hardware designs for FSRI.

a) The first directly implements the funnel shift using combinational logic.

b) The second tries to reuse the existing multiplier hardware to save some area.

The direct version gives the better ML-KEM result and keeps the clock frequency fairly close to the normal processor, but it uses more LUTs. The multiplier-reuse version uses somewhat less area but makes the longest path through the processor much slower.

The current RTL contains the multiplier-reuse version. The direct FSRI results are kept in `results/summary.json` and correspond to commit `1b1d01aaaffe48a0bfff3cdc096cca526f8a40ca`.

# Testing and Results

I care about two things here. One is whether the instructions reduce the number of cycles needed for complete ML-KEM. The other is what they cost in actual hardware.

The cycle numbers below add together key generation, encapsulation, and decapsulation.

| Parameter set | Original `mlkem-native` | Best software version |      FQMUL |      RED32 |    FSRI direct | FSRI multiplier reuse |
| ------------- | ----------------------: | --------------------: | ---------: | ---------: | -------------: | --------------------: |
| ML-KEM-512    |              13,004,560 |            12,636,735 | 11,703,506 | 11,847,177 |  **8,700,799** |             9,055,759 |
| ML-KEM-768    |              20,658,560 |            20,143,356 | 19,023,552 | 19,187,466 | **13,604,448** |            14,189,088 |
| ML-KEM-1024   |              31,479,592 |            30,964,081 | 29,318,051 | 29,614,042 | **20,711,107** |            21,634,003 |

Before adding hardware I could already save around 2% of the cycles just by rearranging the software operations.

After that:

1. FQMUL saves another 5.32% to 7.39% of the complete ML-KEM cycles depending on the parameter set.

2. RED32 saves another 4.36% to 6.25%.

3. The direct FSRI design saves around 31.15% to 33.11%.

The result I found most interesting was FSRI. The two instructions aimed directly at the polynomial arithmetic definitely help, but the improvement is much smaller once I compare them against software that has already been rearranged reasonably well.

FSRI instead attacks the 64-bit rotations inside Keccak. On a 32-bit processor those rotations are expensive enough that replacing them has a much larger effect on complete ML-KEM.

I also checked the generated disassembly and the number of retired custom instructions to make sure this result was actually coming from replacing the Keccak rotation code and not from a problem with the benchmark counters.

## Hardware area and delay

Reducing the number of cycles is only half the problem. A new instruction means adding actual hardware to the processor. This can increase the FPGA area and can also make the longest path through the processor slower, which means the processor cannot be clocked as fast.

I use Yosys to synthesize each design and nextpnr to place and route it on an ECP5 FPGA.

LUT4s are the main configurable logic blocks on the FPGA, so more LUTs roughly means more hardware area. `Fmax` is the highest clock frequency the routed design can meet, so higher is better.

I also run five different place-and-route seeds rather than reporting one lucky placement.

| Design                |            LUT4 |     Flip-flops | DSP | BRAM |         Median Fmax | 50 MHz seeds |
| --------------------- | --------------: | -------------: | --: | ---: | ------------------: | -----------: |
| normal PicoRV32       |           3,583 |            970 |   4 |    0 |           68.70 MHz |          5/5 |
| FQMUL                 |  3,788 (+5.72%) | 1,053 (+8.56%) |   4 |    0 | 60.51 MHz (-11.92%) |          5/5 |
| RED32                 |  3,816 (+6.50%) | 1,053 (+8.56%) |   4 |    0 | 60.20 MHz (-12.37%) |          5/5 |
| FSRI direct           | 4,018 (+12.14%) |   970 (+0.00%) |   4 |    0 |  66.72 MHz (-2.88%) |          5/5 |
| FSRI multiplier reuse |  3,905 (+8.99%) | 1,037 (+6.91%) |   4 |    0 | 50.25 MHz (-26.86%) |          3/5 |

The direct FSRI design gives the best cycle result and barely changes the clock frequency, but it uses 12.14% more LUTs.

I then tried reusing the multiplier to reduce this cost. That brings the LUT increase down to 8.99%, but the timing becomes much worse. Only three out of the five FPGA placements can still meet 50 MHz.

FQMUL and RED32 use less area than direct FSRI, but both still add more hardware than I originally wanted and both reduce the maximum clock frequency by around 12%.

So under the hardware limits I originally set, none of the three instructions passes every requirement.

That does not mean the experiment failed. FSRI still gives the strongest reduction in complete ML-KEM cycles by a large margin. The issue is that the direct version costs too much area, while the smaller multiplier-reuse version gives up too much timing.

The complete machine-readable results are in [`results/summary.json`](results/summary.json).

## Testing

I tested the project at several levels because saving cycles is not useful if the arithmetic or the hardware is wrong.

1. The different software implementations are compared against the reference ML-KEM arithmetic.

2. Complete key generation, encapsulation, and decapsulation are run and the resulting shared secret is checked.

3. The custom instructions are tested directly under Verilator. These tests cover the arithmetic, fixed latency, reset, back-to-back instructions, and instruction decoding.

4. Every hardware design is synthesized and placed/routed five times on the ECP5 target so the reported area and timing numbers are not coming from one placement.

5. FSRI also has targeted formal checks for its PCPI behavior and for how the instruction retires through PicoRV32.

The targeted FSRI PCPI bounded model check passes. The RVFI harness had a later reset-gating fix and still needs to be rerun before I claim a final RVFI PASS. So I am not calling the whole design formally verified.

The formal files are under [`targets/picorv32/formal/`](targets/picorv32/formal/) and the FSRI task is [`targets/picorv32/formal/fsri.sby`](targets/picorv32/formal/fsri.sby).

All performance numbers above are processor-cycle counts from running PicoRV32 RTL under Verilator. They are not host-machine timings.

# Citations

[1] NIST, FIPS 203, Module-Lattice-Based Key-Encapsulation Mechanism Standard.

[2] NIST, FIPS 202, SHA-3 Standard: Permutation-Based Hash and Extendable-Output Functions.

[3] Keccak-f[1600], as specified by FIPS 202.

[4] RISC-V Instruction Set Manual and RISC-V custom instruction encoding documentation.

[5] `mlkem-native`, the ML-KEM implementation used by this project. The pinned revision used in the experiments is `69d24e37b8a04c6050ec55bc84a4228d7051bb4b`.

[6] E. Alkim, H. Evkan, N. Lahr, R. Niederhagen, and R. Petri, "ISA Extensions for Finite Field Arithmetic: Accelerating Kyber and NewHope on RISC-V," TCHES 2020.

[7] RISC-V Bitmanip v0.93 funnel-shift specification.

[8] PicoRV32, pinned at `a473fc8fca393771d83b0ffcf0b14db3393339d8`.

[9] Verilator, Yosys, and nextpnr, used for RTL simulation, synthesis, and ECP5 place and route.
