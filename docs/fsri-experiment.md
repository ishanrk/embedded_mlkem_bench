# rv32 fsri experiment

## instruction

The experiment uses one custom RV32 instruction family:

```text
fsri rd, rs1, rs2, shamt
rd = low32(({rs2, rs1} >> shamt))
```

`shamt` is restricted to `0..31`. The encoding is `custom-0` (`0x0b`),
`funct3=2`, with `shamt` in bits `29:25` and bits `31:30` clear. Swapping
`rs1` and `rs2` produces the other half needed by a 64-bit rotate, so each
Keccak rotation uses two instructions.

This is a restricted adaptation of the experimental Zbt `fsri` instruction in
RISC-V Bitmanip v0.93. It keeps the published funnel-shift semantics but uses a
project-local encoding and only the immediate range needed by RV32 Keccak.

## implementation

The area-oriented design reuses the PCPI multiplier already required for the
standard RISC-V M instructions. For `0 < s < 32`, let `f = 2^(32-s)`. Then

```text
rs1 >> s        = high32(rs1 * f)
rs2 << (32 - s) = low32(rs2 * f)
```

The result is the bitwise OR of those two nonoverlapping halves. `s=0` returns
`rs1` through the same fixed-latency state sequence.

A small decoder registers `f` before the multiplier is used. The first multiply
produces the low part of `rs1 >> s`; the second produces the high part from
`rs2`. The direct PCPI response occurs after exactly three rising edges for all
shift amounts. On the pinned dual-port PicoRV32 this corresponds to an expected
six processor cycles per FSRI. There is still only one multiply expression in
the PCPI block, so synthesis must not add DSPs.

The measured combinational implementation at commit
`1b1d01aaaffe48a0bfff3cdc096cca526f8a40ca` remains in
`results/fsri-combinational.json` as the speed and hardware reference. It used a
second combinational barrel network and measured 31.15%, 32.46%, and 33.11%
fewer cycles than searched software, but cost 12.14% more LUT4s and lost 2.88%
median Fmax.

The software integration still replaces only `MLK_KECCAK_ROL` in the pinned
`mlkem-native` Keccak source. Polynomial arithmetic, ML-KEM APIs, test vectors,
and the standard M path are unchanged.

## model

The pinned Keccak code executes 29 64-bit rotations per round and 24 rounds,
for 696 rotations per permutation. The minimum complete ML-KEM workloads execute
85, 140, and 221 permutations for ML-KEM-512, ML-KEM-768, and ML-KEM-1024.

Relative to the measured three-cycle combinational FSRI, the six-cycle
multiplier-reuse implementation adds three cycles per dynamic FSRI. The minimum
workload model therefore predicts 28.34%, 29.56%, and 30.13% fewer cycles than
searched software, retaining about 91% of the combinational gain. Verilator
measurements are authoritative because rejection-sampling retries can increase
the dynamic instruction count.

## acceptance

The implementation is a feasible merge candidate only when all of these hold:

- every complete operation is faster than searched software
- no complete operation regresses by more than 2%
- median LUT and flip-flop increases are each at most 5%
- median post-route Fmax loss is at most 2%
- every seed meets 50 MHz
- DSP and BRAM counts do not increase

The report separately compares cycles and hardware against the combinational
FSRI, FQMUL, and RED32. Beating the combinational FSRI in cycles is not required;
meeting the hardware budget is the purpose of this implementation.

## prior work

- RISC-V Bitmanip v0.93, funnel-shift specification:
  https://github.com/riscv/riscv-bitmanip/blob/v0.93/texsrc/bext.tex
- RISC-V scalar cryptography rationale, SHA3 rotations on RV32:
  https://github.com/riscv/riscv-crypto/blob/9cb90879050f68de8ba0b2f8bc34bec599ac8025/doc/scalar/riscv-crypto-scalar-appx-rationale.adoc
- pinned PicoRV32 core:
  https://github.com/YosysHQ/picorv32/tree/a473fc8fca393771d83b0ffcf0b14db3393339d8
