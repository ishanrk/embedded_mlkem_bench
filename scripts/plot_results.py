#!/usr/bin/env python3

import json
from pathlib import Path

import matplotlib.pyplot as plt

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "results" / "current-comparison.json"
OUTPUT = ROOT / "docs" / "figures" / "end-to-end-cycles.svg"


def main():
    data = json.loads(DATA.read_text())
    levels = ("512", "768", "1024")
    names = (
        ("portable", "mlkem-native portable"),
        ("software", "best software schedule"),
        ("fqmul", "FQMUL"),
        ("red32", "RED32"),
    )

    x = list(range(len(levels)))
    width = 0.19
    offsets = (-1.5 * width, -0.5 * width, 0.5 * width, 1.5 * width)

    plt.rcParams["svg.fonttype"] = "none"
    fig, ax = plt.subplots(figsize=(8.0, 4.6))

    for (key, label), offset in zip(names, offsets):
        values = [data["levels"][level][key]["total"] / 1_000_000 for level in levels]
        bars = ax.bar([value + offset for value in x], values, width, label=label)
        for bar, value in zip(bars, values):
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                value + 0.22,
                f"{value:.2f}",
                ha="center",
                va="bottom",
                fontsize=8,
            )

    ax.set_xticks(x, [f"ML-KEM-{level}" for level in levels])
    ax.set_ylabel("keygen + encapsulation + decapsulation (million CPU cycles)")
    ax.set_ylim(0, 35)
    ax.legend(frameon=False, ncol=2)
    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    fig.tight_layout()

    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUTPUT, format="svg", bbox_inches="tight", metadata={"Date": None})
    plt.close(fig)


if __name__ == "__main__":
    main()
