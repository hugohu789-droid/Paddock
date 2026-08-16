#!/usr/bin/env python3
"""Turn a NIWA CliFlo export into a Paddock weather snapshot.

CliFlo (https://cliflo.niwa.co.nz) is free but needs a registered account, and
it delivers one file per datatype: rainfall in one export, temperature extremes
in another, radiation and wind in others. This script merges those exports into
the single daily table core/SnapshotWeather.hpp replays, checks that the result
has one row per day with no gaps, and records the SHA-256 that a scenario bundle
pins.

    python scripts/cliflo-snapshot.py \\
        --out data/snapshots/lincoln-2023.csv \\
        --dataset "Lincoln, Canterbury (agent 4881)" \\
        rain.csv temperature.csv radiation.csv

Downloading is deliberately left to you: CliFlo's terms tie access to your own
account, and a script that drives someone's login is a script that eventually
does it without them noticing. What is automated is the part that has to be
reproducible - the conversion and the hash.

The snapshot itself is never committed (data/snapshots/ is gitignored). What
goes into the repository is this script and the hash, which is what makes a
bundle reproducible without carrying the data.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import re
import sys
from pathlib import Path

# CliFlo column headings, per datatype, as observed in its CSV exports. These
# are PLACEHOLDER mappings until they have been checked against a real export of
# each datatype - see docs/verify.md, item 7. When a heading is not recognised
# the script prints what it actually found rather than guessing, and --column
# lets you map it by hand.
COLUMN_PATTERNS: dict[str, list[str]] = {
    "rainfall_mm": [r"^amount\(mm\)$", r"^rain.*\(mm\)$"],
    "min_air_temperature_c": [r"^tmin\(c\)$", r"^minimum.*temp.*"],
    "max_air_temperature_c": [r"^tmax\(c\)$", r"^maximum.*temp.*"],
    "solar_radiation_mj_per_m2": [r"^amount\(mj/m2\)$", r"^global.*rad.*"],
    "wind_speed_m_per_s": [r"^speed\(m/s\)$", r"^mean.*speed.*"],
}

DATE_PATTERNS = [r"^date\(nzst\)$", r"^date.*", r"^observation.*date.*"]

REQUIRED = ("rainfall_mm", "min_air_temperature_c", "max_air_temperature_c")
OPTIONAL = ("solar_radiation_mj_per_m2", "wind_speed_m_per_s")
FIELDS = ("date",) + REQUIRED + OPTIONAL


def normalise(heading: str) -> str:
    return re.sub(r"\s+", "", heading.strip().lower())


def match_field(heading: str) -> str | None:
    cleaned = normalise(heading)
    for pattern in DATE_PATTERNS:
        if re.match(pattern, cleaned):
            return "date"
    for field, patterns in COLUMN_PATTERNS.items():
        for pattern in patterns:
            if re.match(pattern, cleaned):
                return field
    return None


def parse_date(text: str) -> dt.date:
    """CliFlo stamps daily rows with a date and often a time; keep the date."""
    cleaned = text.strip().split()[0]
    for fmt in ("%Y%m%d", "%Y-%m-%d", "%d/%m/%Y"):
        try:
            return dt.datetime.strptime(cleaned, fmt).date()
        except ValueError:
            continue
    raise SystemExit(f"cannot read '{text}' as a date (tried YYYYMMDD, ISO, DD/MM/YYYY)")


def read_export(path: Path, overrides: dict[str, str]) -> dict[dt.date, dict[str, float]]:
    """Read one CliFlo export, returning {date: {field: value}}.

    CliFlo files carry a preamble and a trailing note, so the header row is
    found by looking for the first row that maps to a date column.
    """
    rows: dict[dt.date, dict[str, float]] = {}
    with path.open(newline="", encoding="utf-8-sig") as handle:
        mapping: dict[int, str] = {}
        for line_number, fields in enumerate(csv.reader(handle), start=1):
            if not fields:
                continue
            if not mapping:
                candidate = {}
                for index, heading in enumerate(fields):
                    field = overrides.get(normalise(heading)) or match_field(heading)
                    if field:
                        candidate[index] = field
                if "date" in candidate.values():
                    mapping = candidate
                    unmapped = [h for i, h in enumerate(fields) if i not in mapping]
                    print(f"{path.name}: header on line {line_number}, "
                          f"mapped {sorted(set(mapping.values()))}"
                          + (f", ignored {unmapped}" if unmapped else ""))
                continue

            record: dict[str, float] = {}
            date: dt.date | None = None
            for index, field in mapping.items():
                if index >= len(fields):
                    continue
                value = fields[index].strip()
                if field == "date":
                    try:
                        date = parse_date(value)
                    except SystemExit:
                        date = None  # trailing note rather than a data row
                    continue
                if value == "":
                    continue
                try:
                    record[field] = float(value)
                except ValueError:
                    continue
            if date is not None and record:
                rows.setdefault(date, {}).update(record)

    if not mapping:
        raise SystemExit(
            f"{path}: no date column recognised. Headings seen in this file were not "
            f"matched; map them with --column 'HEADING=field' (fields: {', '.join(FIELDS)})."
        )
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("exports", nargs="+", type=Path, help="CliFlo CSV exports to merge")
    parser.add_argument("--out", required=True, type=Path, help="snapshot to write")
    parser.add_argument("--dataset", default="", help="station description, for the bundle")
    parser.add_argument("--column", action="append", default=[],
                        metavar="HEADING=FIELD", help="map an unrecognised heading by hand")
    arguments = parser.parse_args()

    overrides: dict[str, str] = {}
    for override in arguments.column:
        heading, _, field = override.partition("=")
        if field not in FIELDS:
            raise SystemExit(f"unknown field '{field}'; choose one of {', '.join(FIELDS)}")
        overrides[normalise(heading)] = field

    merged: dict[dt.date, dict[str, float]] = {}
    for export in arguments.exports:
        for date, record in read_export(export, overrides).items():
            merged.setdefault(date, {}).update(record)

    if not merged:
        raise SystemExit("no daily rows found in the exports")

    days = sorted(merged)
    missing_required = [
        (day, field) for day in days for field in REQUIRED if field not in merged[day]
    ]
    if missing_required:
        day, field = missing_required[0]
        raise SystemExit(
            f"{day} has no {field} ({len(missing_required)} such gaps). Include the export "
            f"that carries it, or trim the range with a shorter CliFlo request."
        )

    gaps = [
        (days[i - 1], days[i])
        for i in range(1, len(days))
        if (days[i] - days[i - 1]).days != 1
    ]
    if gaps:
        before, after = gaps[0]
        raise SystemExit(
            f"the series jumps from {before} to {after} ({len(gaps)} gaps). Paddock replays "
            f"one row per day: a missing day is indistinguishable from a dry one."
        )

    arguments.out.parent.mkdir(parents=True, exist_ok=True)
    with arguments.out.open("w", newline="\n", encoding="utf-8") as handle:
        if arguments.dataset:
            handle.write(f"# station: {arguments.dataset}\n")
        handle.write("# generated by scripts/cliflo-snapshot.py from "
                     f"{', '.join(export.name for export in arguments.exports)}\n")
        handle.write(",".join(FIELDS) + "\n")
        for day in days:
            record = merged[day]
            values = [day.isoformat()] + [
                f"{record.get(field, 0.0):.6g}" for field in FIELDS[1:]
            ]
            handle.write(",".join(values) + "\n")

    digest = hashlib.sha256(arguments.out.read_bytes()).hexdigest()
    arguments.out.with_suffix(arguments.out.suffix + ".sha256").write_text(
        f"{digest}  {arguments.out.name}\n", encoding="utf-8"
    )

    print(f"wrote {arguments.out} - {len(days)} days, {days[0]} to {days[-1]}")
    print(f"sha256 {digest}")
    print("Pin that hash in the scenario bundle; the snapshot itself is not committed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
