#!/usr/bin/env python3
"""Plot a furble companion power CSV as a self-contained SVG.

The CSV is produced by:

  furble-companion-cli monitor --csv --duration 3600 ...

Only battery percentage and battery millivolts are plotted. Some firmware has
no PMIC current backend and reports 0 mA; this tool never derives a current from
percentage or voltage. Rows with an empty battery_percent are reconnect/event
markers, not zero-valued samples.
"""

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from xml.sax.saxutils import escape as _xml_escape

REQUIRED_COLUMNS = (
    "host_time_iso",
    "host_elapsed_s",
    "battery_percent",
    "battery_mv",
)
WIDTH = 1000
HEIGHT = 720
LEFT = 92
RIGHT = 36
TOP = 92
PANEL_HEIGHT = 228
PANEL_GAP = 58


def escape(value: str) -> str:
    """Escape XML text and the double quote used by SVG attributes."""
    return _xml_escape(value, {'"': "&quot;", "'": "&apos;"})


@dataclass
class Sample:
    elapsed: float
    timestamp: str
    uptime_s: float | None
    battery_percent: int
    battery_mv: int
    battery_ma: int | None
    gps_source: int | None
    gps_satellites: int | None
    control_state: int | None
    rssi: int | None
    label: str


@dataclass
class Marker:
    elapsed: float
    event: str


@dataclass
class Run:
    samples: list[Sample] = field(default_factory=list)
    markers: list[Marker] = field(default_factory=list)
    labels: list[tuple[float, str]] = field(default_factory=list)
    title: str = ""
    all_current_missing: bool = True
    warnings: list[str] = field(default_factory=list)


class CsvError(ValueError):
    pass


def _parse_float(value: str | None) -> float | None:
    if value is None:
        return None
    text = value.strip()
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def _parse_int(value: str | None) -> int | None:
    number = _parse_float(value)
    if number is None:
        return None
    return int(number)


def _iso_elapsed(value: str, first: datetime | None) -> float | None:
    text = (value or "").strip()
    if not text:
        return None
    try:
        parsed = datetime.fromisoformat(text.replace("Z", "+00:00"))
    except ValueError:
        return None
    if first is None:
        return 0.0
    return (parsed - first).total_seconds()


def load_csv(path: str | Path) -> Run:
    """Load a monitor CSV. Raises CsvError on a missing required column."""
    with open(path, newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames is None:
            raise CsvError("CSV is empty")
        missing = [name for name in REQUIRED_COLUMNS if name not in reader.fieldnames]
        if missing:
            raise CsvError("CSV is missing columns: " + ", ".join(missing))

        run = Run()
        first_timestamp: datetime | None = None
        first_elapsed: float | None = None
        previous_label = ""
        for raw in reader:
            timestamp = (raw.get("host_time_iso") or "").strip()
            if timestamp:
                try:
                    parsed = datetime.fromisoformat(timestamp.replace("Z", "+00:00"))
                    if first_timestamp is None:
                        first_timestamp = parsed
                except ValueError:
                    pass

            elapsed = _parse_float(raw.get("host_elapsed_s"))
            if elapsed is None:
                elapsed = _iso_elapsed(timestamp, first_timestamp)
            if elapsed is None:
                continue
            if first_elapsed is None:
                first_elapsed = elapsed
            elapsed = max(0.0, elapsed - first_elapsed)

            label = (raw.get("label") or "").strip()
            percent = _parse_int(raw.get("battery_percent"))
            millivolts = _parse_int(raw.get("battery_mv"))
            if percent is None or millivolts is None:
                event = label or "event"
                run.markers.append(Marker(elapsed=elapsed, event=event))
                continue
            if percent < 0 or percent > 100:
                run.markers.append(Marker(elapsed=elapsed, event="battery-unknown"))
                continue

            if label and label != previous_label:
                run.labels.append((elapsed, label))
                previous_label = label

            current = _parse_int(raw.get("battery_ma"))
            if current is not None and current != 0:
                run.all_current_missing = False
            run.samples.append(
                Sample(
                    elapsed=elapsed,
                    timestamp=timestamp,
                    uptime_s=_parse_float(raw.get("uptime_s")),
                    battery_percent=percent,
                    battery_mv=millivolts,
                    battery_ma=current,
                    gps_source=_parse_int(raw.get("gps_source")),
                    gps_satellites=_parse_int(raw.get("gps_satellites")),
                    control_state=_parse_int(raw.get("control_state")),
                    rssi=_parse_int(raw.get("rssi")),
                    label=label,
                )
            )

    if not run.samples:
        raise CsvError("CSV contains no battery samples")
    return run


def _scale(value: float, old_min: float, old_max: float, new_min: float, new_max: float) -> float:
    if old_max <= old_min:
        return (new_min + new_max) / 2.0
    ratio = (value - old_min) / (old_max - old_min)
    return new_min + ratio * (new_max - new_min)


def _nice_ticks(low: float, high: float, count: int = 5) -> list[float]:
    if high <= low:
        high = low + 1.0
    step = (high - low) / count
    return [low + step * index for index in range(count + 1)]


def _polyline(
    samples: list[Sample],
    value,
    x0: float,
    x1: float,
    y0: float,
    y1: float,
    x_low: float,
    x_high: float,
    value_low: float,
    value_high: float,
) -> str:
    points = []
    for sample in samples:
        x = _scale(sample.elapsed, x_low, x_high, x0, x1)
        y = _scale(value(sample), value_low, value_high, y1, y0)
        points.append(f"{x:.1f},{y:.1f}")
    return " ".join(points)


def _panel(
    parts: list[str],
    samples: list[Sample],
    value,
    label: str,
    unit: str,
    x0: float,
    x1: float,
    y0: float,
    y1: float,
    x_low: float,
    x_high: float,
    value_low: float,
    value_high: float,
    color: str,
    markers: list[Marker],
    label_changes: list[tuple[float, str]],
) -> None:
    parts.append(
        f'<rect x="{x0:.1f}" y="{y0:.1f}" width="{x1 - x0:.1f}" '
        f'height="{y1 - y0:.1f}" fill="#ffffff" stroke="#c9d1d9"/>'
    )

    for tick in _nice_ticks(value_low, value_high, 5):
        y = _scale(tick, value_low, value_high, y1, y0)
        parts.append(
            f'<line x1="{x0:.1f}" y1="{y:.1f}" x2="{x1:.1f}" y2="{y:.1f}" '
            'stroke="#edf1f5" stroke-width="1"/>'
        )
        parts.append(
            f'<text x="{x0 - 12:.1f}" y="{y + 4:.1f}" text-anchor="end" '
            f'font-size="12" fill="#4a5568">{escape(f"{tick:.1f}{unit}")}</text>'
        )

    for tick in _nice_ticks(x_low, x_high, 6):
        x = _scale(tick, x_low, x_high, x0, x1)
        parts.append(
            f'<line x1="{x:.1f}" y1="{y0:.1f}" x2="{x:.1f}" y2="{y1:.1f}" '
            'stroke="#edf1f5" stroke-width="1"/>'
        )
        parts.append(
            f'<text x="{x:.1f}" y="{y1 + 20:.1f}" text-anchor="middle" '
            f'font-size="12" fill="#4a5568">{escape(f"{tick / 60:.0f}")}</text>'
        )

    points = _polyline(
        samples, value, x0, x1, y0, y1, x_low, x_high, value_low, value_high
    )
    parts.append(
        f'<polyline points="{points}" fill="none" stroke="{color}" '
        'stroke-width="2.4" stroke-linejoin="round" stroke-linecap="round"/>'
    )
    if len(samples) == 1:
        x = _scale(samples[0].elapsed, x_low, x_high, x0, x1)
        y = _scale(value(samples[0]), value_low, value_high, y1, y0)
        parts.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="3.5" fill="{color}"/>')

    parts.append(
        f'<text x="{x0:.1f}" y="{y0 - 12:.1f}" font-size="14" '
        f'font-weight="600" fill="#1f2937">{escape(label)}</text>'
    )
    parts.append(
        f'<text x="{x1:.1f}" y="{y1 + 38:.1f}" text-anchor="end" '
        'font-size="12" fill="#4a5568">elapsed minutes</text>'
    )

    for marker in markers:
        x = _scale(marker.elapsed, x_low, x_high, x0, x1)
        color_for_marker = "#d97706" if "reconnect" in marker.event or "link" in marker.event else "#7c3aed"
        parts.append(
            f'<line x1="{x:.1f}" y1="{y0:.1f}" x2="{x:.1f}" y2="{y1:.1f}" '
            f'stroke="{color_for_marker}" stroke-width="1.2" stroke-dasharray="4 4"/>'
        )

    for index, (elapsed, changed) in enumerate(label_changes):
        x = _scale(elapsed, x_low, x_high, x0, x1)
        parts.append(
            f'<line x1="{x:.1f}" y1="{y0:.1f}" x2="{x:.1f}" y2="{y1:.1f}" '
            'stroke="#0f766e" stroke-width="1.2" stroke-dasharray="2 3"/>'
        )
        if y0 > TOP + 20:
            parts.append(
                f'<text x="{x + 4:.1f}" y="{y0 + 14 + (index % 3) * 14:.1f}" '
                f'font-size="11" fill="#0f766e">{escape(changed)}</text>'
            )


def build_svg(run: Run, title: str = "") -> str:
    samples = run.samples
    x_high = max(sample.elapsed for sample in samples)
    if run.markers:
        x_high = max(x_high, max(marker.elapsed for marker in run.markers))
    if x_high <= 0:
        x_high = 1.0
    x_low = 0.0

    values = [sample.battery_mv for sample in samples]
    mv_low = min(values)
    mv_high = max(values)
    if mv_high <= mv_low:
        mv_high = mv_low + 20
        mv_low = max(0, mv_low - 20)
    else:
        padding = max(10, (mv_high - mv_low) * 0.12)
        mv_low = max(0, int(mv_low - padding))
        mv_high = int(mv_high + padding)

    x0 = LEFT
    x1 = WIDTH - RIGHT
    top_y0 = TOP
    top_y1 = TOP + PANEL_HEIGHT
    bottom_y0 = top_y1 + PANEL_GAP
    bottom_y1 = bottom_y0 + PANEL_HEIGHT

    shown_title = title or run.title or "furble companion battery run"
    marker_rows = max(1, (len(run.markers) + 3) // 4)
    height = HEIGHT + (marker_rows - 1) * 14
    parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {WIDTH} {height}" '
        f'width="{WIDTH}" height="{height}" role="img" '
        f'aria-label="{escape(shown_title)}">',
        '<rect width="100%" height="100%" fill="#f8fafc"/>',
        f'<text x="{LEFT}" y="38" font-size="22" font-weight="700" '
        f'fill="#111827">{escape(shown_title)}</text>',
        f'<text x="{LEFT}" y="62" font-size="12" fill="#4a5568">'
        f'{len(samples)} samples · {x_high / 60:.1f} minutes · '
        f'percent {samples[0].battery_percent}%→{samples[-1].battery_percent}% · '
        f'{samples[0].battery_mv}mV→{samples[-1].battery_mv}mV</text>',
    ]
    if run.all_current_missing:
        parts.append(
            f'<text x="{LEFT}" y="78" font-size="12" fill="#b45309">'
            "battery_ma is 0/absent: firmware has no PMIC current backend; "
            "use the percent and millivolt slope.</text>"
        )

    _panel(
        parts, samples, lambda sample: sample.battery_percent,
        "Battery percentage", "%", x0, x1, top_y0, top_y1, x_low, x_high,
        0, 100, "#2563eb", run.markers, run.labels,
    )
    _panel(
        parts, samples, lambda sample: sample.battery_mv,
        "Battery voltage", " mV", x0, x1, bottom_y0, bottom_y1, x_low, x_high,
        float(mv_low), float(mv_high), "#b91c1c", run.markers, run.labels,
    )

    marker_text_y = bottom_y1 + 58
    for index, marker in enumerate(run.markers):
        row = index // 4
        parts.append(
            f'<text x="{LEFT + (index % 4) * 225}" y="{marker_text_y + row * 14}" '
            f'font-size="11" fill="#6b7280">{escape(marker.event)} @ '
            f'{marker.elapsed / 60:.1f} min</text>'
        )
    parts.append("</svg>")
    return "\n".join(parts) + "\n"


def write_png(run: Run, path: str | Path, title: str = "") -> bool:
    """Write a PNG if matplotlib is available. SVG remains the primary output."""
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        return False

    samples = run.samples
    elapsed = [sample.elapsed / 60.0 for sample in samples]
    figure, axes = plt.subplots(2, 1, figsize=(10, 6.8), sharex=True)
    axes[0].plot(elapsed, [sample.battery_percent for sample in samples], color="#2563eb")
    axes[0].set_ylabel("Battery (%)")
    axes[0].set_ylim(0, 100)
    axes[0].grid(True, alpha=0.25)
    axes[1].plot(elapsed, [sample.battery_mv for sample in samples], color="#b91c1c")
    axes[1].set_ylabel("Battery (mV)")
    axes[1].set_xlabel("Elapsed minutes")
    axes[1].grid(True, alpha=0.25)
    for axis in axes:
        for marker in run.markers:
            axis.axvline(marker.elapsed / 60.0, color="#d97706", linestyle=":", linewidth=1)
        for change_at, change_label in run.labels:
            axis.axvline(change_at / 60.0, color="#0f766e", linestyle="--", linewidth=1)
            axis.text(change_at / 60.0, 0.98, change_label, transform=axis.get_xaxis_transform(),
                      fontsize=8, color="#0f766e", va="top", ha="left")
    figure.suptitle(title or run.title or "furble companion battery run")
    figure.tight_layout()
    figure.savefig(path)
    plt.close(figure)
    return True


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Plot a furble companion power CSV as a self-contained SVG."
    )
    parser.add_argument("csv", help="CSV produced by furble-companion-cli monitor --csv")
    parser.add_argument("-o", "--output", help="SVG output path (default: input .svg)")
    parser.add_argument("--title", default="", help="SVG/PNG title")
    parser.add_argument(
        "--png",
        help="Also write a PNG if matplotlib is installed; SVG is always written",
    )
    arguments = parser.parse_args(argv)

    try:
        run = load_csv(arguments.csv)
    except (OSError, CsvError) as error:
        print(f"plot_companion_power: {error}", file=sys.stderr)
        return 2

    output = Path(arguments.output) if arguments.output else Path(arguments.csv).with_suffix(".svg")
    output.write_text(build_svg(run, arguments.title), encoding="utf-8")
    print(
        f"wrote {output} ({len(run.samples)} samples, {len(run.markers)} markers)",
        file=sys.stderr,
    )

    if arguments.png:
        if write_png(run, arguments.png, arguments.title):
            print(f"wrote {arguments.png}", file=sys.stderr)
        else:
            print(
                "matplotlib is not installed; kept the SVG output only",
                file=sys.stderr,
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
