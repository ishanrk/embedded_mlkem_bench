#!/usr/bin/env python3
"""Regenerate the technical figures used by README.md.

The measured values come from results/summary.json.  SVG is written directly so
the documentation build needs only Python's standard library.
"""

from __future__ import annotations

import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results" / "summary.json"
OUTPUT = ROOT / "docs" / "figures"

COLORS = {
    "ink": "#172033",
    "muted": "#5f6b7a",
    "grid": "#d7dde7",
    "panel": "#f7f9fc",
    "blue": "#3568d4",
    "red": "#c84b4b",
    "green": "#27866f",
    "orange": "#d27a22",
    "purple": "#7056b8",
}


def header(width: int, height: int, title: str, description: str) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
        f"<title id=\"title\">{title}</title>",
        f"<desc id=\"desc\">{description}</desc>",
        f'<rect width="{width}" height="{height}" fill="white"/>',
        "<style>",
        "text { font-family: ui-sans-serif, system-ui, -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif; }",
        ".title { font-size: 25px; font-weight: 700; fill: #172033; }",
        ".subtitle { font-size: 15px; fill: #5f6b7a; }",
        ".panel-title { font-size: 21px; font-weight: 700; fill: #172033; }",
        ".label { font-size: 16px; fill: #172033; }",
        ".small { font-size: 14px; fill: #5f6b7a; }",
        ".value { font-size: 15px; font-weight: 650; fill: #172033; }",
        ".mono { font-family: ui-monospace, SFMono-Regular, Menlo, Consolas, monospace; }",
        "</style>",
    ]


def write_svg(name: str, lines: list[str]) -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    lines.append("</svg>")
    (OUTPUT / name).write_text("\n".join(lines) + "\n", encoding="utf-8")


def arrow(lines: list[str], x1: float, y: float, x2: float) -> None:
    ink = COLORS["muted"]
    lines.append(f'<line x1="{x1}" y1="{y}" x2="{x2 - 8}" y2="{y}" stroke="{ink}" stroke-width="2"/>')
    lines.append(f'<path d="M {x2 - 8} {y - 5} L {x2} {y} L {x2 - 8} {y + 5} Z" fill="{ink}"/>')


def instruction_figure() -> None:
    width, height = 1200, 430
    lines = header(
        width,
        height,
        "Three custom instruction designs",
        "Comparison of inputs, exact operations, outputs, and measured direct PCPI latency.",
    )
    lines += [
        '<text class="title" x="40" y="42">Three custom instruction designs</text>',
        '<text class="subtitle" x="40" y="68">All use custom-0. The current RTL reuses one shared multiplier expression.</text>',
    ]

    panels = [
        (40, COLORS["blue"], "A", "mlk.fqmul"),
        (420, COLORS["red"], "B", "mlk.red32"),
        (800, COLORS["green"], "C", "fsri"),
    ]
    for x, color, letter, name in panels:
        lines += [
            f'<rect x="{x}" y="92" width="350" height="282" rx="12" fill="{COLORS["panel"]}" stroke="{COLORS["grid"]}"/>',
            f'<circle cx="{x + 27}" cy="122" r="15" fill="{color}"/>',
            f'<text x="{x + 27}" y="127" text-anchor="middle" font-size="14" font-weight="700" fill="white">{letter}</text>',
            f'<text class="panel-title mono" x="{x + 53}" y="129">{name}</text>',
        ]

    # FQMUL panel.
    lines += [
        '<rect x="62" y="162" width="87" height="57" rx="7" fill="white" stroke="#9cacbf"/>',
        '<text class="value mono" x="105" y="185" text-anchor="middle">a, b</text>',
        '<text class="small" x="105" y="205" text-anchor="middle">signed low 16</text>',
        '<rect x="181" y="151" width="128" height="79" rx="7" fill="#e9f0ff" stroke="#3568d4"/>',
        '<text class="value" x="245" y="180" text-anchor="middle">multiply</text>',
        '<text class="value" x="245" y="202" text-anchor="middle">+ Montgomery</text>',
        '<text class="small" x="245" y="220" text-anchor="middle">q = 3329</text>',
        '<rect x="326" y="162" width="46" height="57" rx="7" fill="white" stroke="#9cacbf"/>',
        '<text class="value mono" x="349" y="195" text-anchor="middle">rd</text>',
        '<text class="label mono" x="62" y="267">rd = RED(a × b)</text>',
        '<text class="small" x="62" y="294">Used in NTT, inverse NTT, base</text>',
        '<text class="small" x="62" y="314">multiplication, caches, and conversion.</text>',
        '<text class="value" x="62" y="348">4 PCPI response edges</text>',
    ]
    arrow(lines, 149, 190, 181)
    arrow(lines, 309, 190, 326)

    # RED32 panel.
    lines += [
        '<rect x="442" y="162" width="104" height="57" rx="7" fill="white" stroke="#9cacbf"/>',
        '<text class="value mono" x="494" y="185" text-anchor="middle">t = rs1</text>',
        '<text class="small" x="494" y="205" text-anchor="middle">signed 32 bit</text>',
        '<rect x="578" y="151" width="115" height="79" rx="7" fill="#fff0ef" stroke="#c84b4b"/>',
        '<text class="value" x="635" y="180" text-anchor="middle">Montgomery</text>',
        '<text class="value" x="635" y="202" text-anchor="middle">reduction</text>',
        '<text class="small" x="635" y="220" text-anchor="middle">q = 3329</text>',
        '<rect x="710" y="162" width="42" height="57" rx="7" fill="white" stroke="#9cacbf"/>',
        '<text class="value mono" x="731" y="195" text-anchor="middle">rd</text>',
        '<text class="label mono" x="442" y="267">rd = RED(t)</text>',
        '<text class="small" x="442" y="294">An ordinary RISC-V MUL computes t.</text>',
        '<text class="small" x="442" y="314">Only the reduction moves to hardware.</text>',
        '<text class="value" x="442" y="348">3 PCPI response edges</text>',
    ]
    arrow(lines, 546, 190, 578)
    arrow(lines, 693, 190, 710)

    # FSRI panel.
    lines += [
        '<rect x="822" y="151" width="104" height="79" rx="7" fill="white" stroke="#9cacbf"/>',
        '<text class="value mono" x="874" y="176" text-anchor="middle">rs2, rs1</text>',
        '<text class="small" x="874" y="197" text-anchor="middle">two words</text>',
        '<text class="small mono" x="874" y="217" text-anchor="middle">s[4:0]</text>',
        '<rect x="958" y="151" width="124" height="79" rx="7" fill="#eaf7f3" stroke="#27866f"/>',
        '<text class="value mono" x="1020" y="180" text-anchor="middle">{rs2,rs1}</text>',
        '<text class="value mono" x="1020" y="204" text-anchor="middle">&gt;&gt; s</text>',
        '<text class="small" x="1020" y="222" text-anchor="middle">take low 32</text>',
        '<rect x="1099" y="162" width="58" height="57" rx="7" fill="white" stroke="#9cacbf"/>',
        '<text class="value mono" x="1128" y="195" text-anchor="middle">rd</text>',
        '<text class="label mono" x="822" y="267">rd = low32(({rs2,rs1} &gt;&gt; s))</text>',
        '<text class="small" x="822" y="294">Two FSRI instructions build one 64-bit</text>',
        '<text class="small" x="822" y="314">Keccak rotate from 32-bit halves.</text>',
        '<text class="value" x="822" y="348">3 PCPI edges; 6 core cycles</text>',
    ]
    arrow(lines, 926, 190, 958)
    arrow(lines, 1082, 190, 1099)

    lines += [
        '<text class="small" x="40" y="406">PCPI latency is measured from request acceptance to ready. Whole-core CPI also includes PicoRV32 control overhead.</text>'
    ]
    write_svg("instruction-designs.svg", lines)


def cycle_figure(data: dict) -> None:
    width, height = 1200, 620
    lines = header(
        width,
        height,
        "Complete ML-KEM cycle comparison",
        "Normalized cycle totals for key generation, encapsulation, and decapsulation on three ML-KEM parameter sets.",
    )
    lines += [
        '<text class="title" x="50" y="43">Complete ML-KEM cycles relative to tuned software</text>',
        '<text class="subtitle" x="50" y="70">Each total is keygen median + encapsulation median + decapsulation median. Lower is better.</text>',
    ]

    designs = [
        ("software", "Tuned software", COLORS["muted"]),
        ("fqmul", "mlk.fqmul", COLORS["blue"]),
        ("red32", "mlk.red32", COLORS["red"]),
        ("fsri_multiplier_reuse", "fsri", COLORS["green"]),
    ]
    legend_x = [420, 600, 775, 950]
    for (key, name, color), x in zip(designs, legend_x):
        lines += [
            f'<rect x="{x}" y="91" width="17" height="17" rx="2" fill="{color}"/>',
            f'<text class="small mono" x="{x + 25}" y="105">{name}</text>',
        ]

    plot_left, plot_top, plot_width, plot_height = 100, 140, 1040, 385
    for pct in range(0, 101, 20):
        y = plot_top + plot_height - pct / 105 * plot_height
        lines += [
            f'<line x1="{plot_left}" y1="{y:.1f}" x2="{plot_left + plot_width}" y2="{y:.1f}" stroke="{COLORS["grid"]}"/>',
            f'<text class="small" x="{plot_left - 14}" y="{y + 5:.1f}" text-anchor="end">{pct}%</text>',
        ]
    lines.append(f'<line x1="{plot_left}" y1="{plot_top + plot_height}" x2="{plot_left + plot_width}" y2="{plot_top + plot_height}" stroke="{COLORS["ink"]}" stroke-width="1.5"/>')

    levels = ["512", "768", "1024"]
    group_centers = [285, 620, 955]
    bar_width, gap = 52, 12
    for level, center in zip(levels, group_centers):
        baseline = data["levels"][level]["software"]
        group_width = len(designs) * bar_width + (len(designs) - 1) * gap
        x0 = center - group_width / 2
        for index, (key, _, color) in enumerate(designs):
            ratio = 100.0 * data["levels"][level][key] / baseline
            height_px = ratio / 105 * plot_height
            x = x0 + index * (bar_width + gap)
            y = plot_top + plot_height - height_px
            lines += [
                f'<rect x="{x:.1f}" y="{y:.1f}" width="{bar_width}" height="{height_px:.1f}" rx="3" fill="{color}"/>',
                f'<text class="value" x="{x + bar_width / 2:.1f}" y="{y - 8:.1f}" text-anchor="middle">{ratio:.1f}%</text>',
            ]
        lines += [
            f'<text class="label" x="{center}" y="558" text-anchor="middle">ML-KEM-{level}</text>',
            f'<text class="small" x="{center}" y="582" text-anchor="middle">software total: {baseline:,} cycles</text>',
        ]
    write_svg("mlkem-cycle-comparison.svg", lines)


def hardware_figure(data: dict) -> None:
    width, height = 1200, 665
    lines = header(
        width,
        height,
        "Hardware cost and routed timing",
        "Added LUT and flip-flop percentages plus all five routed Fmax values for the current instruction designs.",
    )
    lines += [
        '<text class="title" x="45" y="42">Hardware cost and routed timing</text>',
        '<text class="subtitle" x="45" y="68">ECP5 LFE5U-45F-6BG381C, five place-and-route seeds. Dashed lines are acceptance limits.</text>',
        '<text class="panel-title" x="45" y="115">Added logic vs baseline</text>',
        '<text class="panel-title" x="655" y="115">Fmax across routing seeds</text>',
    ]

    hardware = data["hardware"]
    designs = [
        ("fqmul", "mlk.fqmul", COLORS["blue"]),
        ("red32", "mlk.red32", COLORS["red"]),
        ("fsri_multiplier_reuse", "fsri", COLORS["green"]),
    ]

    # Added logic grouped horizontal bars.
    chart_x, chart_y, chart_w = 170, 155, 395
    scale = chart_w / 10.0
    for tick in range(0, 11, 2):
        x = chart_x + tick * scale
        lines += [
            f'<line x1="{x:.1f}" y1="{chart_y}" x2="{x:.1f}" y2="485" stroke="{COLORS["grid"]}"/>',
            f'<text class="small" x="{x:.1f}" y="510" text-anchor="middle">{tick}%</text>',
        ]
    limit_x = chart_x + 5 * scale
    lines += [
        f'<line x1="{limit_x:.1f}" y1="{chart_y - 8}" x2="{limit_x:.1f}" y2="485" stroke="{COLORS["orange"]}" stroke-width="2" stroke-dasharray="6 5"/>',
        f'<text class="small" x="{limit_x + 6:.1f}" y="145">+5% limit</text>',
    ]
    for row, (key, name, color) in enumerate(designs):
        y = 205 + row * 100
        item = hardware[key]
        lut = item["lut4_change_percent"]
        ff = item["flip_flop_change_percent"]
        lines += [
            f'<text class="label mono" x="45" y="{y + 22}">{name}</text>',
            f'<rect x="{chart_x}" y="{y}" width="{lut * scale:.1f}" height="25" rx="3" fill="{color}"/>',
            f'<rect x="{chart_x}" y="{y + 34}" width="{ff * scale:.1f}" height="25" rx="3" fill="{color}" opacity="0.55"/>',
            f'<text class="value" x="{chart_x + lut * scale + 8:.1f}" y="{y + 18}">LUT {lut:.2f}%</text>',
            f'<text class="value" x="{chart_x + ff * scale + 8:.1f}" y="{y + 52}">FF {ff:.2f}%</text>',
        ]

    # Fmax distribution.
    fmax_x, fmax_y, fmax_w = 785, 155, 350
    low, high = 40.0, 75.0
    fscale = fmax_w / (high - low)
    for tick in range(40, 76, 5):
        x = fmax_x + (tick - low) * fscale
        lines += [
            f'<line x1="{x:.1f}" y1="{fmax_y}" x2="{x:.1f}" y2="510" stroke="{COLORS["grid"]}"/>',
            f'<text class="small" x="{x:.1f}" y="538" text-anchor="middle">{tick}</text>',
        ]
    target_x = fmax_x + (50.0 - low) * fscale
    lines += [
        f'<line x1="{target_x:.1f}" y1="{fmax_y - 8}" x2="{target_x:.1f}" y2="510" stroke="{COLORS["orange"]}" stroke-width="2" stroke-dasharray="6 5"/>',
        f'<text class="small" x="{target_x + 6:.1f}" y="145">50 MHz target</text>',
        '<text class="small" x="960" y="566" text-anchor="middle">Routed Fmax (MHz)</text>',
    ]
    timing_designs = [
        ("baseline", "Baseline"),
        ("fqmul", "mlk.fqmul"),
        ("red32", "mlk.red32"),
        ("fsri_multiplier_reuse", "fsri"),
    ]
    for row, (key, name) in enumerate(timing_designs):
        y = 205 + row * 82
        item = hardware[key]
        values = item["fmax_by_seed_mhz"]
        median = item["median_fmax_mhz"]
        passed = item["seeds_meeting_50mhz"]
        lines += [
            f'<text class="label mono" x="655" y="{y + 5}">{name}</text>',
            f'<line x1="{fmax_x + (min(values) - low) * fscale:.1f}" y1="{y}" x2="{fmax_x + (max(values) - low) * fscale:.1f}" y2="{y}" stroke="#9cacbf" stroke-width="3"/>',
        ]
        for index, value in enumerate(values):
            x = fmax_x + (value - low) * fscale
            cy = y + ((index % 3) - 1) * 9
            point_color = COLORS["green"] if value >= 50.0 else COLORS["orange"]
            lines.append(f'<circle cx="{x:.1f}" cy="{cy}" r="6" fill="{point_color}" stroke="white" stroke-width="1.5"/>')
        mx = fmax_x + (median - low) * fscale
        lines += [
            f'<path d="M {mx:.1f} {y - 10} L {mx + 8:.1f} {y} L {mx:.1f} {y + 10} L {mx - 8:.1f} {y} Z" fill="{COLORS["ink"]}"/>',
            f'<text class="small" x="{fmax_x + fmax_w}" y="{y + 25}" text-anchor="end">median {median:.2f}; {passed}/5 pass</text>',
        ]

    lines += [
        f'<circle cx="680" cy="605" r="6" fill="{COLORS["green"]}"/><text class="small" x="693" y="610">seed meets 50 MHz</text>',
        f'<circle cx="855" cy="605" r="6" fill="{COLORS["orange"]}"/><text class="small" x="868" y="610">seed misses 50 MHz</text>',
        f'<path d="M 1036 595 L 1044 605 L 1036 615 L 1028 605 Z" fill="{COLORS["ink"]}"/><text class="small" x="1051" y="610">median</text>',
        '<text class="small" x="45" y="645">All four designs use 4 DSP blocks and 0 BRAM. None of the three custom designs passes the complete area-and-timing budget.</text>',
    ]
    write_svg("hardware-tradeoff.svg", lines)


def validate(data: dict) -> None:
    for level, item in data["levels"].items():
        for design, operations in item["operation_cycles"].items():
            total = sum(operations.values())
            if total != item[design]:
                raise ValueError(f"ML-KEM-{level} {design}: operations sum to {total}, expected {item[design]}")
    for name, item in data["hardware"].items():
        if "fmax_by_seed_mhz" not in item:
            continue
        values = sorted(item["fmax_by_seed_mhz"])
        median = values[len(values) // 2]
        if not math.isclose(median, item["median_fmax_mhz"], abs_tol=0.005):
            raise ValueError(f"{name}: median Fmax does not match seed values")
        passed = sum(value >= data["platform"]["target_frequency_mhz"] for value in values)
        if passed != item["seeds_meeting_50mhz"]:
            raise ValueError(f"{name}: 50 MHz pass count does not match seed values")


def main() -> None:
    data = json.loads(RESULTS.read_text(encoding="utf-8"))
    validate(data)
    instruction_figure()
    cycle_figure(data)
    hardware_figure(data)
    print(f"wrote README figures to {OUTPUT.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
