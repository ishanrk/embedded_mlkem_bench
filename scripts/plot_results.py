#!/usr/bin/env python3

import json
from pathlib import Path

import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "results" / "current-comparison.json"
OUTPUT = ROOT / "docs" / "figures" / "cycle-savings.svg"


def main():
    data = json.loads(DATA.read_text())
    levels = ("512", "768", "1024")
    fqmul = [
        data["levels"][level]["fqmul"]["fewer_cycles_percent"] for level in levels
    ]
    red32 = [
        data["levels"][level]["red32"]["fewer_cycles_percent"] for level in levels
    ]

    x = list(range(len(levels)))
    width = 0.34
    left = [value - width / 2 for value in x]
    right = [value + width / 2 for value in x]

    plt.rcParams["svg.fonttype"] = "none"
    fig, ax = plt.subplots(figsize=(7.2, 4.2))
    ax.bar(left, fqmul, width, label="FQMUL")
    ax.bar(right, red32, width, label="RED32")
    ax.set_xticks(x, [f"ML-KEM-{level}" for level in levels])
    ax.set_ylabel("fewer cycles than searched software (%)")
    ax.set_ylim(0, max(fqmul + red32) + 1.0)
    ax.legend(frameon=False)

    for positions, values in ((left, fqmul), (right, red32)):
        for position, value in zip(positions, values):
            ax.text(
                position, value + 0.08, f"{value:.2f}%",
                ha="center", va="bottom", fontsize=9,
            )

    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    fig.tight_layout()
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUTPUT, format="svg", bbox_inches="tight", metadata={"Date": None})
    plt.close(fig)


if __name__ == "__main__":
    main()
