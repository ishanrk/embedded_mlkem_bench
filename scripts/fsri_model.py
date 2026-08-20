#!/usr/bin/env python3

import json

BASE = {"512": 12636735, "768": 20143356, "1024": 30964081}
FQMUL = {"512": 7.385, "768": 5.559, "1024": 5.316}
PARAMS = {
    "512": (2, 800, 768, 3),
    "768": (3, 1184, 1088, 2),
    "1024": (4, 1568, 1568, 2),
}


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
        for cycles in range(7):
            saved = perms * 696 * max(0, 18 - 2 * cycles)
            gain = 100.0 * saved / BASE[level]
            sweep[str(cycles)] = {
                "saved_cycles": saved,
                "gain_percent": gain,
                "beats_fqmul": gain > FQMUL[level],
            }
        levels[level] = {
            "k": k,
            "matrix_min_permutations": matrix,
            "noise_permutations": noise,
            "hash_permutations": hashes,
            "minimum_permutations": perms,
            "baseline_cycles": BASE[level],
            "fqmul_gain_percent": FQMUL[level],
            "latency_sweep": sweep,
        }
    print(json.dumps({
        "schema": "pqc-poly-bench/fsri-model-v1",
        "candidate": "fsri",
        "rotations_per_permutation": 696,
        "baseline_cycles_per_rotation": 18,
        "fsri_per_rotation": 2,
        "levels": levels,
        "decision": "rtl_test",
    }, indent=2))


if __name__ == "__main__":
    main()
