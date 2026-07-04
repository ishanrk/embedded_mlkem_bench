#!/usr/bin/env python3

import json

SOFTWARE = {"512": 12636735, "768": 20143356, "1024": 30964081}
COMBINATIONAL = {"512": 8700799, "768": 13604448, "1024": 20711107}
PARAMS = {
    "512": (2, 800, 768, 3),
    "768": (3, 1184, 1088, 2),
    "1024": (4, 1568, 1568, 2),
}
ROTATIONS = 696
FSRI_PER_ROTATION = 2
COMBINATIONAL_PCPI_CPI = 3
MULTIPLIER_REUSE_PCPI_CPI = 6


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
        dynamic = perms * ROTATIONS * FSRI_PER_ROTATION
        added = dynamic * (MULTIPLIER_REUSE_PCPI_CPI - COMBINATIONAL_PCPI_CPI)
        total = COMBINATIONAL[level] + added
        gain = 100.0 * (SOFTWARE[level] - total) / SOFTWARE[level]
        fast_gain = (
            100.0 * (SOFTWARE[level] - COMBINATIONAL[level]) / SOFTWARE[level]
        )
        levels[level] = {
            "k": k,
            "matrix_min_permutations": matrix,
            "noise_permutations": noise,
            "hash_permutations": hashes,
            "minimum_permutations": perms,
            "minimum_dynamic_fsri": dynamic,
            "software_total_cycles": SOFTWARE[level],
            "measured_combinational_total_cycles": COMBINATIONAL[level],
            "multiplier_reuse_prediction": {
                "added_cycles_vs_combinational": added,
                "total_cycles": total,
                "gain_vs_software_percent": gain,
                "retained_combinational_gain_percent": 100.0 * gain / fast_gain,
            },
        }
    print(
        json.dumps(
            {
                "schema": "pqc-poly-bench/fsri-model-v3",
                "candidate": "fsri",
                "rotations_per_permutation": ROTATIONS,
                "fsri_per_rotation": FSRI_PER_ROTATION,
                "pico_rv32_cpi": {
                    "combinational_pcpi_instruction": COMBINATIONAL_PCPI_CPI,
                    "multiplier_reuse_pcpi_instruction": MULTIPLIER_REUSE_PCPI_CPI,
                },
                "levels": levels,
                "decision": "measure_multiplier_reuse",
            },
            indent=2,
        )
    )


if __name__ == "__main__":
    main()
