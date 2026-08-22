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

The PCPI result is combinational and has no FSRI state or result registers. On
the pinned dual-port PicoRV32, the core still observes that response after the
PCPI request becomes valid, so the expected total instruction cost is three
processor cycles. The previous registered response cost one additional cycle.

The software integration replaces only `MLK_KECCAK_ROL` in the pinned
`mlkem-native` Keccak source. Polynomial arithmetic, ML-KEM APIs, test vectors,
and the M-extension path are unchanged.

## model

The pinned Keccak code executes 29 64-bit rotations per round and 24 rounds,
for 696 rotations per permutation. The minimum complete ML-KEM workloads in the
model execute 85, 140, and 221 permutations for ML-KEM-512, ML-KEM-768, and
ML-KEM-1024.

At three cycles per FSRI, the static model predicts reductions of 5.62%, 5.80%,
and 5.96%. These are workload bounds, not measured results. Verilator
measurements are authoritative.

## acceptance

The experiment is a feasible merge candidate only when all of these hold:

- every complete operation is faster than searched software
- no complete operation regresses by more than 2%
- median LUT and flip-flop increases are each at most 5%
- median post-route Fmax loss is at most 2%
- every seed meets 50 MHz
- DSP and BRAM counts do not increase

The report separately records whether FSRI beats FQMUL and RED32 at every ML-KEM
level. Those instructions are cycle references but their measured hardware
implementations already fail the project budget.

## prior work

- RISC-V Bitmanip v0.93, funnel-shift specification:
  https://github.com/riscv/riscv-bitmanip/blob/v0.93/texsrc/bext.tex
- RISC-V scalar cryptography rationale, SHA3 rotations on RV32:
  https://github.com/riscv/riscv-crypto/blob/9cb90879050f68de8ba0b2f8bc34bec599ac8025/doc/scalar/riscv-crypto-scalar-appx-rationale.adoc
- pinned PicoRV32 core:
  https://github.com/YosysHQ/picorv32/tree/a473fc8fca393771d83b0ffcf0b14db3393339d8
