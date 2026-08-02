#!/usr/bin/env python3

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/figures"
SUMMARY = ROOT / "results/summary.json"
VARIANTS = ("baseline", "fqmul", "red32", "fsri", "dot2x")
LABELS = {"baseline": "Baseline", "fqmul": "FQMUL", "red32": "RED32", "fsri": "FSRI", "dot2x": "DOT2X"}
COLORS = {"baseline": "#5f6b7a", "fqmul": "#3568d4", "red32": "#c84b4b", "fsri": "#27866f", "dot2x": "#8356b6"}


def start(width, height, title, description):
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        f'<title id="title">{title}</title>',
        f'<desc id="desc">{description}</desc>',
        f'<rect width="{width}" height="{height}" fill="white"/>',
        '<style>text{font-family:system-ui,sans-serif;fill:#172033}.title{font-size:25px;font-weight:700}.small{font-size:14px;fill:#5f6b7a}.label{font-size:16px}.value{font-size:14px;font-weight:650}.mono{font-family:ui-monospace,monospace}</style>',
    ]


def write(name, lines):
    lines.append("</svg>")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / name).write_text("\n".join(lines) + "\n", encoding="utf-8")


def pending(lines, width, height):
    lines.extend(
        [
            f'<rect x="70" y="110" width="{width - 140}" height="{height - 180}" rx="12" fill="#f7f9fc" stroke="#d7dde7"/>',
            f'<text x="{width / 2}" y="{height / 2}" text-anchor="middle" class="title">pending fair rerun</text>',
            f'<text x="{width / 2}" y="{height / 2 + 32}" text-anchor="middle" class="small">old planner measurements are intentionally not reused</text>',
        ]
    )


def legend(lines, y=80):
    for index, variant in enumerate(VARIANTS):
        x = 330 + index * 170
        lines.append(f'<rect x="{x}" y="{y - 14}" width="16" height="16" fill="{COLORS[variant]}"/>')
        lines.append(f'<text x="{x + 23}" y="{y}" class="small">{LABELS[variant]}</text>')


def instruction_designs():
    lines = start(1500, 420, "Custom instruction designs", "FQMUL RED32 FSRI and DOT2X data paths")
    lines.extend(
        [
            '<text x="40" y="42" class="title">Four custom instruction designs</text>',
            '<text x="40" y="68" class="small">The baseline keeps these four decoders disabled</text>',
        ]
    )
    panels = (
        (40, "FQMUL", "signed low halves", "multiply then Montgomery reduce", "4 cycle response", COLORS["fqmul"]),
        (405, "RED32", "signed product in rs1", "Montgomery reduce only", "3 cycle response", COLORS["red32"]),
        (770, "FSRI", "joined rs2 and rs1", "shift then take low word", "direct response", COLORS["fsri"]),
        (1135, "DOT2X", "two signed halves", "two crossed products", "3 cycle response", COLORS["dot2x"]),
    )
    for x, name, inputs, operation, latency, color in panels:
        lines.extend(
            [
                f'<rect x="{x}" y="100" width="350" height="260" rx="12" fill="#f7f9fc" stroke="#d7dde7"/>',
                f'<rect x="{x + 24}" y="132" width="92" height="58" rx="7" fill="white" stroke="#aab5c5"/>',
                f'<rect x="{x + 140}" y="122" width="130" height="78" rx="7" fill="white" stroke="{color}" stroke-width="2"/>',
                f'<rect x="{x + 294}" y="132" width="36" height="58" rx="7" fill="white" stroke="#aab5c5"/>',
                f'<text x="{x + 24}" y="130" class="title">{name}</text>',
                f'<text x="{x + 70}" y="158" text-anchor="middle" class="small">inputs</text>',
                f'<text x="{x + 205}" y="153" text-anchor="middle" class="value">{operation}</text>',
                f'<text x="{x + 312}" y="166" text-anchor="middle" class="mono">rd</text>',
                f'<path d="M{x + 116} 161H{x + 140}M{x + 270} 161H{x + 294}" stroke="#5f6b7a" stroke-width="2"/>',
                f'<text x="{x + 24}" y="240" class="label">{inputs}</text>',
                f'<text x="{x + 24}" y="278" class="value">{latency}</text>',
            ]
        )
    lines.append('<text x="40" y="397" class="small">All five processors use the same PCPI multiplier for ordinary RV32M multiplication</text>')
    write("instruction-designs.svg", lines)


def cycle_figure(data):
    lines = start(1200, 590, "ML KEM cycle comparison", "Complete operation cycle totals for five variants")
    lines.append('<text x="50" y="42" class="title">Complete ML KEM cycle comparison</text>')
    legend(lines)
    if data["status"] != "complete":
        pending(lines, 1200, 590)
        write("mlkem-cycle-comparison.svg", lines)
        return
    maximum = max(
        data["measurements"][variant][level]["total_cycles"]
        for variant in VARIANTS
        for level in ("512", "768", "1024")
    )
    for level_index, level in enumerate(("512", "768", "1024")):
        y = 145 + level_index * 130
        lines.append(f'<text x="50" y="{y + 28}" class="label">ML KEM {level}</text>')
        for variant_index, variant in enumerate(VARIANTS):
            value = data["measurements"][variant][level]["total_cycles"]
            width = 780 * value / maximum
            bar_y = y + variant_index * 22
            lines.append(f'<rect x="190" y="{bar_y}" width="{width:.2f}" height="16" fill="{COLORS[variant]}"/>')
            lines.append(f'<text x="{200 + width:.2f}" y="{bar_y + 13}" class="small">{value:,}</text>')
    write("mlkem-cycle-comparison.svg", lines)


def area_figure(data):
    lines = start(1200, 540, "Complete core area", "ECP5 LUT4 and flip flop counts")
    lines.append('<text x="50" y="42" class="title">Complete PicoRV32 core area</text>')
    legend(lines)
    if data["status"] != "complete":
        pending(lines, 1200, 540)
        write("area.svg", lines)
        return
    for field_index, field in enumerate(("lut4", "flip_flops", "dsp", "bram")):
        values = [data["hardware"][variant][field] for variant in VARIANTS]
        maximum = max(max(values), 1)
        x = 80 + field_index * 275
        lines.append(f'<text x="{x}" y="125" class="label">{field}</text>')
        for variant_index, variant in enumerate(VARIANTS):
            value = values[variant_index]
            height = 300 * value / maximum
            bar_x = x + variant_index * 46
            lines.append(f'<rect x="{bar_x}" y="{450 - height:.2f}" width="34" height="{height:.2f}" fill="{COLORS[variant]}"/>')
            lines.append(f'<text x="{bar_x + 17}" y="{470 - height:.2f}" text-anchor="middle" class="small">{value}</text>')
    write("area.svg", lines)


def fmax_figure(data):
    lines = start(1200, 520, "Routed maximum frequency", "Five nextpnr seeds and their median")
    lines.append('<text x="50" y="42" class="title">Routed maximum frequency across five seeds</text>')
    legend(lines)
    if data["status"] != "complete":
        pending(lines, 1200, 520)
        write("fmax.svg", lines)
        return
    frequencies = [value for variant in VARIANTS for value in data["hardware"][variant]["fmax_by_seed_mhz"]]
    maximum = max(frequencies) * 1.1
    for variant_index, variant in enumerate(VARIANTS):
        x = 90 + variant_index * 220
        lines.append(f'<text x="{x}" y="130" class="label">{LABELS[variant]}</text>')
        for seed_index, value in enumerate(data["hardware"][variant]["fmax_by_seed_mhz"]):
            y = 450 - value / maximum * 280
            lines.append(f'<circle cx="{x + 25 + seed_index * 35}" cy="{y:.2f}" r="8" fill="{COLORS[variant]}"/>')
            lines.append(f'<text x="{x + 25 + seed_index * 35}" y="{y - 13:.2f}" text-anchor="middle" class="small">{value}</text>')
    write("fmax.svg", lines)


def tradeoff_figure(data):
    lines = start(1200, 560, "Hardware tradeoff", "Average cycle change against LUT4 change")
    lines.append('<text x="50" y="42" class="title">Cycle saving versus LUT4 cost</text>')
    if data["status"] != "complete":
        pending(lines, 1200, 560)
        write("hardware-tradeoff.svg", lines)
        return
    base_lut = data["hardware"]["baseline"]["lut4"]
    lines.extend(
        [
            '<path d="M100 460H1120M100 100V460" stroke="#5f6b7a" stroke-width="2"/>',
            '<text x="610" y="515" text-anchor="middle" class="label">LUT4 change percent</text>',
            '<text x="25" y="280" transform="rotate(-90 25 280)" text-anchor="middle" class="label">cycle saving percent</text>',
        ]
    )
    for variant in VARIANTS[1:]:
        lut_change = (data["hardware"][variant]["lut4"] / base_lut - 1) * 100
        saving = -sum(
            data["measurements"][variant][level]["percent_change_vs_baseline"]
            for level in ("512", "768", "1024")
        ) / 3
        x = 120 + min(max(lut_change, 0), 30) / 30 * 950
        y = 440 - min(max(saving, 0), 50) / 50 * 320
        lines.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="11" fill="{COLORS[variant]}"/>')
        lines.append(f'<text x="{x + 16:.2f}" y="{y - 8:.2f}" class="label">{LABELS[variant]}</text>')
    write("hardware-tradeoff.svg", lines)


def main():
    data = json.loads(SUMMARY.read_text(encoding="utf-8"))
    instruction_designs()
    cycle_figure(data)
    area_figure(data)
    fmax_figure(data)
    tradeoff_figure(data)


if __name__ == "__main__":
    main()
