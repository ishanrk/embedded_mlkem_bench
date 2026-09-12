#!/usr/bin/env python3

import html
import json
import pathlib
import statistics


ROOT = pathlib.Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs/figures"
SUMMARY = ROOT / "results/summary.json"
WIDTH = 1400
HEIGHT = 760
VARIANTS = ("baseline", "fqmul", "red32", "fsri", "dot2x")
LEVELS = ("512", "768", "1024")
LABELS = {
    "baseline": "Baseline",
    "fqmul": "FQMUL",
    "red32": "RED32",
    "fsri": "FSRI",
    "dot2x": "DOT2X",
}
COLORS = {
    "baseline": "#687386",
    "fqmul": "#316bd6",
    "red32": "#c64d4d",
    "fsri": "#27866f",
    "dot2x": "#8356b6",
}


def escape(value):
    return html.escape(str(value), quote=True)


def start(title, description):
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}" role="img" aria-labelledby="title desc">',
        f'<title id="title">{escape(title)}</title>',
        f'<desc id="desc">{escape(description)}</desc>',
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="white"/>',
        '<style>text{font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;fill:#172033}.title{font-size:28px;font-weight:700}.subtitle{font-size:15px;fill:#5c6678}.variant{font-size:15px;font-weight:650}.group{font-size:17px;font-weight:650}.value{font-family:ui-monospace,"SFMono-Regular",Consolas,monospace;font-size:12px;font-weight:650}.change{font-size:12px;fill:#667085}.pending{font-size:12px;font-weight:650;fill:#8992a3;letter-spacing:.04em}.note{font-size:13px;fill:#667085}</style>',
        f'<text x="56" y="48" class="title">{escape(title)}</text>',
        f'<text x="56" y="78" class="subtitle">{escape(description)}</text>',
    ]


def write(name, lines):
    lines.append("</svg>")
    OUTPUT.mkdir(parents=True, exist_ok=True)
    (OUTPUT / name).write_text("\n".join(lines) + "\n", encoding="utf-8")


def is_number(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool)


def measured_cycles(data, variant, level, field):
    measurement = data["measurements"][variant][level]
    value = measurement.get(field)
    if measurement.get("verified") is True and is_number(value) and value > 0:
        return value
    return None


def hardware_value(data, variant, field):
    value = data["hardware"][variant].get(field)
    if is_number(value) and value >= 0:
        return value
    return None


def percent_change(value, baseline):
    if value is None or baseline is None or baseline == 0:
        return None
    change = (value / baseline - 1.0) * 100.0
    if abs(change) < 0.005:
        change = 0.0
    return f"{change:+.2f}%"


def format_integer(value):
    return f"{value:,}"


def format_frequency(value):
    return f"{value:.2f} MHz"


def draw_pending_slot(lines, x, width, bottom):
    lines.extend(
        [
            f'<rect x="{x:.2f}" y="{bottom - 44}" width="{width:.2f}" height="44" rx="4" fill="#f8fafc" stroke="#c8cfda" stroke-dasharray="5 4"/>',
            f'<text x="{x + width / 2:.2f}" y="{bottom - 17}" text-anchor="middle" class="pending">PENDING</text>',
        ]
    )


def draw_single_bars(name, title, subtitle, values, formatter, note=None):
    lines = start(title, subtitle)
    top = 135
    bottom = 535
    centers = (150, 425, 700, 975, 1250)
    bar_width = 132
    present = [value for value in values if value is not None]
    maximum = max(present) if present else None
    baseline = values[0]
    lines.append(
        f'<path d="M70 {bottom}.5H1330" stroke="#98a2b3" stroke-width="1"/>'
    )
    for variant, center, value in zip(VARIANTS, centers, values):
        x = center - bar_width / 2
        if value is None:
            draw_pending_slot(lines, x, bar_width, bottom)
        else:
            height = (bottom - top - 35) * value / maximum
            y = bottom - height
            lines.append(
                f'<rect x="{x:.2f}" y="{y:.2f}" width="{bar_width}" height="{height:.2f}" rx="4" fill="{COLORS[variant]}"/>'
            )
            lines.append(
                f'<text x="{center}" y="{y - 11:.2f}" text-anchor="middle" class="value">{escape(formatter(value))}</text>'
            )
        lines.append(
            f'<text x="{center}" y="570" text-anchor="middle" class="variant">{LABELS[variant]}</text>'
        )
        change = percent_change(value, baseline)
        if variant == "baseline" and value is not None:
            lines.append(
                f'<text x="{center}" y="593" text-anchor="middle" class="change">baseline</text>'
            )
        elif change is not None:
            lines.append(
                f'<text x="{center}" y="593" text-anchor="middle" class="change">{change} vs baseline</text>'
            )
    if not present:
        lines.append(
            '<text x="700" y="655" text-anchor="middle" class="note">No measured values are available yet</text>'
        )
    if note:
        lines.append(
            f'<text x="700" y="716" text-anchor="middle" class="note">{escape(note)}</text>'
        )
    write(name, lines)


def grouped_bars(name, title, subtitle, groups, formatter, note=None):
    lines = start(title, subtitle)
    top = 135
    bottom = 515
    group_count = len(groups)
    group_width = 1260 / group_count
    if group_count == 1:
        content_width = 900
    elif group_count == 2:
        content_width = 550
    else:
        content_width = 410
    slot_width = content_width / len(VARIANTS)
    bar_width = min(92, slot_width - 22)
    present = [value for _, values in groups for value in values if value is not None]
    maximum = max(present) if present else None
    lines.append(
        f'<path d="M70 {bottom}.5H1330" stroke="#98a2b3" stroke-width="1"/>'
    )
    for group_index, (group_label, values) in enumerate(groups):
        center = 70 + group_width * (group_index + 0.5)
        start_x = center - content_width / 2
        baseline = values[0]
        for variant_index, (variant, value) in enumerate(zip(VARIANTS, values)):
            bar_center = start_x + slot_width * (variant_index + 0.5)
            x = bar_center - bar_width / 2
            if value is None:
                draw_pending_slot(lines, x, bar_width, bottom)
            else:
                height = (bottom - top - 35) * value / maximum
                y = bottom - height
                lines.append(
                    f'<rect x="{x:.2f}" y="{y:.2f}" width="{bar_width:.2f}" height="{height:.2f}" rx="3" fill="{COLORS[variant]}"/>'
                )
                lines.append(
                    f'<text x="{bar_center:.2f}" y="{y - 10:.2f}" text-anchor="middle" class="value">{escape(formatter(value))}</text>'
                )
            lines.append(
                f'<text x="{bar_center:.2f}" y="548" text-anchor="middle" class="variant">{LABELS[variant]}</text>'
            )
            change = percent_change(value, baseline)
            if variant == "baseline" and value is not None:
                lines.append(
                    f'<text x="{bar_center:.2f}" y="569" text-anchor="middle" class="change">baseline</text>'
                )
            elif change is not None:
                lines.append(
                    f'<text x="{bar_center:.2f}" y="569" text-anchor="middle" class="change">{change}</text>'
                )
        lines.append(
            f'<text x="{center:.2f}" y="614" text-anchor="middle" class="group">{escape(group_label)}</text>'
        )
    if note:
        lines.append(
            f'<text x="700" y="716" text-anchor="middle" class="note">{escape(note)}</text>'
        )
    write(name, lines)


def total_cycles(data):
    groups = []
    pending_levels = []
    for level in LEVELS:
        values = [
            measured_cycles(data, variant, level, "total_cycles")
            for variant in VARIANTS
        ]
        if any(value is not None for value in values):
            groups.append((f"ML-KEM-{level}", values))
        else:
            pending_levels.append(f"ML-KEM-{level}")
    if not groups:
        values = [None for _ in VARIANTS]
        draw_single_bars(
            "total-cycles.svg",
            "Complete ML-KEM cycle count",
            "Lower is better · Only verified complete-operation totals are shown",
            values,
            format_integer,
            "Pending parameter sets: " + ", ".join(pending_levels),
        )
        return
    note = None
    if pending_levels:
        note = "Pending parameter sets: " + ", ".join(pending_levels)
    grouped_bars(
        "total-cycles.svg",
        "Complete ML-KEM cycle count",
        "Lower is better · Missing variant measurements are marked pending",
        groups,
        format_integer,
        note,
    )


def operation_cycles(data):
    fields = (
        ("Key generation", "keygen_cycles"),
        ("Encapsulation", "encapsulation_cycles"),
        ("Decapsulation", "decapsulation_cycles"),
    )
    completeness = {}
    for level in LEVELS:
        completeness[level] = sum(
            measured_cycles(data, variant, level, field) is not None
            for _, field in fields
            for variant in VARIANTS
        )
    level = max(LEVELS, key=lambda candidate: completeness[candidate])
    groups = []
    for label, field in fields:
        values = [
            measured_cycles(data, variant, level, field) for variant in VARIANTS
        ]
        groups.append((label, values))
    grouped_bars(
        "operation-cycles.svg",
        f"ML-KEM-{level} operation cycle count",
        "Lower is better · The parameter set with the most verified measurements is shown",
        groups,
        format_integer,
        "Pending labels indicate operations without a verified cycle measurement",
    )


def lut4_area(data):
    values = [hardware_value(data, variant, "lut4") for variant in VARIANTS]
    draw_single_bars(
        "lut4-area.svg",
        "Complete PicoRV32 core LUT4 area",
        "Lower is better · Complete routed core with the selected PCPI hardware",
        values,
        format_integer,
    )


def flip_flops(data):
    values = [
        hardware_value(data, variant, "flip_flops") for variant in VARIANTS
    ]
    dsp_values = [hardware_value(data, variant, "dsp") for variant in VARIANTS]
    subtitle = "Lower is better · Complete routed core with the selected PCPI hardware"
    if all(value == 4 for value in dsp_values):
        subtitle += " · All five variants use the same 4 DSP blocks"
    draw_single_bars(
        "flip-flops.svg",
        "Complete PicoRV32 core flip-flop area",
        subtitle,
        values,
        format_integer,
    )


def fmax(data):
    title = "Routed maximum frequency"
    subtitle = "Higher is better · Bars show the median and open circles show individual routing seeds"
    lines = start(title, subtitle)
    top = 135
    bottom = 535
    centers = (150, 425, 700, 975, 1250)
    bar_width = 116
    medians = [
        hardware_value(data, variant, "median_fmax_mhz") for variant in VARIANTS
    ]
    seed_values = []
    for variant in VARIANTS:
        seeds = data["hardware"][variant].get("fmax_by_seed_mhz", [])
        valid = [value for value in seeds if is_number(value) and value > 0]
        if len(valid) != len(seeds) or len(valid) > 5:
            raise ValueError(f"invalid Fmax seeds for {variant}")
        seed_values.append(valid)
    for variant, median, seeds in zip(VARIANTS, medians, seed_values):
        if len(seeds) == 5 and median is not None:
            if abs(statistics.median(seeds) - median) > 1e-9:
                raise ValueError(f"median Fmax does not match the seeds for {variant}")
    present = [value for value in medians if value is not None]
    present.extend(value for seeds in seed_values for value in seeds)
    maximum = max(present) if present else None
    baseline = medians[0]
    lines.append(
        f'<path d="M70 {bottom}.5H1330" stroke="#98a2b3" stroke-width="1"/>'
    )
    for variant, center, median, seeds in zip(
        VARIANTS, centers, medians, seed_values
    ):
        x = center - bar_width / 2 - 18
        if median is None:
            draw_pending_slot(lines, x, bar_width, bottom)
        else:
            height = (bottom - top - 35) * median / maximum
            y = bottom - height
            lines.append(
                f'<rect x="{x:.2f}" y="{y:.2f}" width="{bar_width}" height="{height:.2f}" rx="4" fill="{COLORS[variant]}"/>'
            )
            lines.append(
                f'<text x="{x + bar_width / 2:.2f}" y="{y - 11:.2f}" text-anchor="middle" class="value">{escape(format_frequency(median))}</text>'
            )
        if seeds:
            marker_x = center + 66
            seed_y = [bottom - (bottom - top - 35) * value / maximum for value in seeds]
            lines.append(
                f'<path d="M{marker_x} {min(seed_y):.2f}V{max(seed_y):.2f}" stroke="{COLORS[variant]}" stroke-width="1.5"/>'
            )
            for seed_index, (value, y) in enumerate(zip(seeds, seed_y), start=1):
                dot_x = marker_x + (seed_index - 3) * 5
                lines.append(
                    f'<circle cx="{dot_x}" cy="{y:.2f}" r="4.5" fill="white" stroke="{COLORS[variant]}" stroke-width="2"><title>Seed {seed_index}: {escape(format_frequency(value))}</title></circle>'
                )
        lines.append(
            f'<text x="{center}" y="570" text-anchor="middle" class="variant">{LABELS[variant]}</text>'
        )
        change = percent_change(median, baseline)
        if variant == "baseline" and median is not None:
            lines.append(
                f'<text x="{center}" y="593" text-anchor="middle" class="change">baseline</text>'
            )
        elif change is not None:
            lines.append(
                f'<text x="{center}" y="593" text-anchor="middle" class="change">{change} vs baseline</text>'
            )
        if len(seeds) != 5:
            seed_status = "seeds pending" if not seeds else f"{len(seeds)} of 5 seeds"
            lines.append(
                f'<text x="{center}" y="620" text-anchor="middle" class="change">{seed_status}</text>'
            )
    if not present:
        lines.append(
            '<text x="700" y="655" text-anchor="middle" class="note">No routed frequency measurements are available yet</text>'
        )
    write("fmax.svg", lines)


def main():
    data = json.loads(SUMMARY.read_text(encoding="utf-8"))
    total_cycles(data)
    operation_cycles(data)
    lut4_area(data)
    flip_flops(data)
    fmax(data)


if __name__ == "__main__":
    main()
