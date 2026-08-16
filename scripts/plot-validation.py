#!/usr/bin/env python3
"""Plot the T3 comparison between modelled and measured pasture growth.

The validation suite writes `seasonal-growth.csv` into the build directory; this
turns it into a PNG that CI keeps as an artifact. The point is that the gap
between model and measurement is visible on every pull request, rather than
being something someone goes looking for at the end of a milestone.

    python scripts/plot-validation.py build/default/validation/seasonal-growth.csv \\
        --out build/default/validation/seasonal-growth.png
"""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # no display in CI
import matplotlib.pyplot as plt  # noqa: E402  (must follow the backend choice)

SERIES_LABELS = {
    "modelled_kg_dm_per_ha_per_day": "Paddock (unfertilised, 20-year mean)",
    "woodlands_zero_n": "Woodlands, 0 kg N/ha (DairyNZ)",
    "lincoln_p21_low_n": "Lincoln P21 Low N, 154 kg N/ha (DairyNZ)",
}
SERIES_STYLE = {
    "modelled_kg_dm_per_ha_per_day": {"color": "#1b5e20", "linewidth": 2.5, "marker": "o"},
    "woodlands_zero_n": {"color": "#ef6c00", "linewidth": 1.8, "marker": "s", "linestyle": "--"},
    "lincoln_p21_low_n": {"color": "#6a1b9a", "linewidth": 1.8, "marker": "^", "linestyle": ":"},
}


def read_series(path: Path) -> tuple[list[str], dict[str, list[float]]]:
    with path.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise SystemExit(f"{path}: no rows")
    months = [row["month"].capitalize() for row in rows]
    series = {
        name: [float(row[name]) for row in rows] for name in rows[0] if name != "month"
    }
    return months, series


def shares(values: list[float]) -> list[float]:
    total = sum(values)
    return [value / total * 100.0 for value in values] if total else [0.0] * len(values)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("csv", type=Path, help="seasonal-growth.csv from the validation suite")
    parser.add_argument("--out", type=Path, required=True, help="PNG to write")
    arguments = parser.parse_args()

    months, series = read_series(arguments.csv)
    figure, (rates, distribution) = plt.subplots(1, 2, figsize=(12, 4.8))

    for name, values in series.items():
        style = SERIES_STYLE.get(name, {})
        label = SERIES_LABELS.get(name, name)
        rates.plot(months, values, label=label, **style)
        distribution.plot(months, shares(values), label=label, **style)

    rates.set_title("Mean daily pasture growth")
    rates.set_ylabel("kg DM/ha/day")
    rates.set_ylim(bottom=0)
    rates.grid(alpha=0.3)
    rates.legend(fontsize=8)

    distribution.set_title("Share of the year's growth")
    distribution.set_ylabel("% of annual growth")
    distribution.set_ylim(bottom=0)
    distribution.grid(alpha=0.3)
    distribution.legend(fontsize=8)

    figure.suptitle(
        "T3 validation: modelled seasonal growth against DairyNZ measured averages\n"
        "The model receives no nitrogen fertiliser; growth parameters are placeholders "
        "(docs/verify.md, E7)",
        fontsize=10,
    )
    figure.tight_layout()
    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(arguments.out, dpi=140)
    print(f"wrote {arguments.out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
