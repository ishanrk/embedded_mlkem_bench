#!/usr/bin/env python3

import json

BASE = {"512": 12636735, "768": 20143356, "1024": 30964081}
FQMUL = {
    "512": 7.385048432209744,
    "768": 5.559172960056904,
    "1024": 5.315933645826595,
}
RED32 = {
    "512": 6.24811709670259,
    "768": 4.745435666231585,
    "1024": 4.360016368643397,
}
PARAMS = {
    "512": (2, 800, 768, 3),
    "768": (3, 1184, 1088, 2),
    "1024": (4, 1568, 1568, 2),
}
ROTATIONS = 696
SOFTWARE_ROTATION_CYCLES = 18
FSRI_PER_ROTATION = 2
COMBINATIONAL_PCPI_CPI = 3
REGISTERED_PCPI_CPI = 4


def blocks(n, rate):
    return (n + rate - 1) // rate


def hash_perms(n):
    return n // 136 + 1


def main():
    levels = {}
    for level, (k, pk, ct, eta1) in PARAMS.items():
        eta1_blocks = blocks(64 * eta1, 136)
        matrix = 9 * k * k
        noise = 4 * k * eta1_blocks + 2 * k + 2
        hashes = 3 + 3 * hash_perms(pk) + hash_perms(32 + ct)
        perms = matrix + noise + hashes
        sweep = {}
        for cpi in range(7):
            saved = perms * ROTATIONS * max(
                0, SOFTWARE_ROTATION_CYCLES - FSRI_PER_ROTATION * cpi
            )
            gain = 100.0 * saved / BASE[level]
            sweep[str(cpi)] = {
                "saved_cycles": saved,
                "gain_percent": gain,
                "beats_fqmul": gain > FQMUL[level],
                "beats_red32": gain > RED32[level],
            }
        levels[level] = {
            "k": k,
            "matrix_min_permutations": matrix,
            "noise_permutations": noise,
            "hash_permutations": hashes,
            "minimum_permutations": perms,
            "baseline_cycles": BASE[level],
            "fqmul_gain_percent": FQMUL[level],
            "red32_gain_percent": RED32[level],
            "latency_sweep": sweep,
            "selected_combinational_pcpi": sweep[str(COMBINATIONAL_PCPI_CPI)],
        }
    print(
        json.dumps(
            {
                "schema": "pqc-poly-bench/fsri-model-v2",
                "candidate": "fsri",
                "rotations_per_permutation": ROTATIONS,
                "baseline_cycles_per_rotation": SOFTWARE_ROTATION_CYCLES,
                "fsri_per_rotation": FSRI_PER_ROTATION,
                "pico_rv32_cpi": {
                    "normal_alu_instruction": 3,
                    "combinational_pcpi_instruction": COMBINATIONAL_PCPI_CPI,
                    "registered_pcpi_instruction": REGISTERED_PCPI_CPI,
                },
                "levels": levels,
                "decision": "rtl_and_synthesis_test",
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
