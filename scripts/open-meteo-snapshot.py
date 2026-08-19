#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Gejile Hu. All rights reserved.

"""Fetch daily weather for one farm from Open-Meteo and write a hashed snapshot.

The sibling of scripts/cliflo-snapshot.py, and it exists because that one
cannot finish the job on its own. CliFlo is the New Zealand record and it is
the better measurement, but it needs a registered account, it delivers one file
per datatype, and **its terms do not let the data be passed on**. So a CliFlo
snapshot can never travel with this repository: a collaborator has to have an
account and repeat the export by hand.

Open-Meteo's historical reanalysis needs no key and no account, and is licensed
CC BY 4.0 - attribution, redistribution and commercial use all permitted. That
is what makes a bundle reproducible by somebody who is not you: this script and
a SHA-256 are enough for them to fetch the same file and prove it is the same.

    python scripts/open-meteo-snapshot.py \\
        --lon 172.44386 --lat -43.64954 \\
        --start 2023-07-01 --end 2024-06-30 \\
        --out data/snapshots/lincoln-2023.csv

**Reanalysis is not observation.** What comes back is ERA5 - a model's estimate
of what the weather was, on a grid tens of kilometres across, not a reading
from a gauge in this paddock. It is the right tool for a farm that has no
station on it and the wrong one for verifying a station that does. The
provenance file records which model answered, so a run can never quietly claim
a measurement it did not have.

The snapshot itself is not committed - data/snapshots/ is gitignored - for the
same reason no snapshot is: what makes a bundle reproducible is the hash, not a
copy of the data in the history.
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import hashlib
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ARCHIVE = "https://archive-api.open-meteo.com/v1/archive"

# **A second endpoint, and a second model.** The archive is ERA5 and carries no
# ultraviolet: asking it for uv_index_max returns a column of nulls with the
# unit "undefined". The air quality API does carry it, from CAMS, which is a
# different reanalysis by a different group. Two models in one file is a thing
# a reader has to be told rather than left to discover, so the provenance
# records which column came from which.
AIR_QUALITY = "https://air-quality-api.open-meteo.com/v1/air-quality"

TIMEOUT_SECONDS = 120

LICENCE = "CC-BY-4.0"
ATTRIBUTION = (
    "Weather data by Open-Meteo.com (https://open-meteo.com), licensed under "
    "CC BY 4.0. Generated using Copernicus Climate Change Service information (ERA5)."
)

# What we ask for, and the column of the snapshot each one becomes.
#
# Open-Meteo reports radiation as a daily sum in MJ/m2 when asked for
# shortwave_radiation_sum, which is already the unit DailyWeather carries, and
# wind at 10 m in m/s. The 10 m height is recorded rather than silently treated
# as the 2 m most agronomic equations expect - see the note in the provenance.
DAILY_VARIABLES = {
    "precipitation_sum": "rainfall_mm",
    "temperature_2m_min": "min_air_temperature_c",
    "temperature_2m_max": "max_air_temperature_c",
    "shortwave_radiation_sum": "solar_radiation_mj_per_m2",
    "wind_speed_10m_max": "wind_speed_m_per_s",
}

COLUMNS = [
    "date",
    "rainfall_mm",
    "min_air_temperature_c",
    "max_air_temperature_c",
    "solar_radiation_mj_per_m2",
    "wind_speed_m_per_s",
    "uv_index",
]


class Failure(Exception):
    """An error with something the user can do about it."""


def fetch_json(url: str) -> dict:
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        body = ""
        try:
            body = error.read().decode("utf-8", "replace")[:400]
        except Exception:  # pragma: no cover - diagnostics only
            pass
        raise Failure(
            f"{ARCHIVE} returned {error.code} {error.reason}"
            + (f"\n  {body}" if body else "")
        ) from None
    except urllib.error.URLError as error:
        raise Failure(f"cannot reach {ARCHIVE}: {error.reason}") from None


def parse_date(text: str, what: str) -> dt.date:
    try:
        return dt.date.fromisoformat(text)
    except ValueError:
        raise Failure(f"{what} must be YYYY-MM-DD, got '{text}'") from None


def rows_from(payload: dict, start: dt.date, end: dt.date) -> list[dict]:
    daily = payload.get("daily")
    if not isinstance(daily, dict):
        raise Failure("the reply carried no 'daily' block, so there is nothing to write")

    times = daily.get("time")
    if not times:
        raise Failure("the reply carried no dates")

    missing = [name for name in DAILY_VARIABLES if name not in daily]
    if missing:
        raise Failure(
            "the reply is missing " + ", ".join(sorted(missing))
            + ". Open-Meteo names its variables in the documentation at "
            "https://open-meteo.com/en/docs/historical-weather-api"
        )

    rows: list[dict] = []
    for index, day in enumerate(times):
        row = {"date": day}
        for source, column in DAILY_VARIABLES.items():
            value = daily[source][index]
            if value is None:
                # A gap is refused rather than filled. A zero here would be a
                # dry, still, sunless day that never happened, and the model
                # would run on it without complaint.
                raise Failure(
                    f"{day}: {source} came back empty. The series has a hole in it, and "
                    "filling it here would invent weather"
                )
            row[column] = value
        rows.append(row)

    if not rows:
        raise Failure("no days came back")

    first = parse_date(rows[0]["date"], "the first day returned")
    last = parse_date(rows[-1]["date"], "the last day returned")
    if first != start or last != end:
        raise Failure(
            f"asked for {start} to {end} and got {first} to {last}. The archive lags real "
            "time by about five days; ask for an earlier end date"
        )
    if len(rows) != (end - start).days + 1:
        raise Failure(
            f"{len(rows)} days came back for a span of {(end - start).days + 1}, so the "
            "series has a gap"
        )
    return rows


def add_uv(rows: list[dict], lat: float, lon: float, start: dt.date, end: dt.date,
           timezone: str) -> None:
    """Adds the day's peak ultraviolet index to each row, in place.

    The daily maximum rather than a mean, because that is what a person is
    exposed to and what a sun drawn on a map should be brightest for. It is
    dimensionless: about 1 in a New Zealand midwinter and above 12 in midsummer,
    which is high by world standards and is why it is worth showing at all.
    """
    query = urllib.parse.urlencode({
        "latitude": lat,
        "longitude": lon,
        "start_date": start.isoformat(),
        "end_date": end.isoformat(),
        "daily": "uv_index_max",
        "timezone": timezone,
    })
    payload = fetch_json(f"{AIR_QUALITY}?{query}")
    daily = payload.get("daily", {})
    values = daily.get("uv_index_max")
    times = daily.get("time")
    if not values or not times:
        raise Failure(
            "the air quality API returned no ultraviolet. It is a different service from the "
            "archive and may not cover this period; re-run without it rather than writing a "
            "column of zeros"
        )

    by_date = dict(zip(times, values))
    for row in rows:
        value = by_date.get(row["date"])
        if value is None:
            raise Failure(
                f"{row['date']}: no ultraviolet index. A gap is refused rather than filled - a "
                "zero here is a sunless day that did not happen"
            )
        row["uv_index"] = value


def write_snapshot(rows: list[dict], out: Path) -> str:
    out.parent.mkdir(parents=True, exist_ok=True)
    with out.open("w", encoding="utf-8", newline="\n") as handle:
        writer = csv.DictWriter(handle, fieldnames=COLUMNS, lineterminator="\n")
        writer.writeheader()
        for row in rows:
            writer.writerow(row)

    digest = hashlib.sha256()
    digest.update(out.read_bytes())
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fetch daily weather for one farm from Open-Meteo, and hash it.")
    parser.add_argument("--lon", type=float, required=True,
                        help="Longitude of the farm, degrees east")
    parser.add_argument("--lat", type=float, required=True,
                        help="Latitude of the farm, degrees north (negative in NZ)")
    parser.add_argument("--start", required=True, help="First day, YYYY-MM-DD")
    parser.add_argument("--end", required=True, help="Last day, YYYY-MM-DD")
    parser.add_argument("--timezone", default="Pacific/Auckland",
                        help="The day boundary to aggregate on. A daily total is only "
                             "meaningful once you say when the day starts, and using UTC for a "
                             "New Zealand farm puts half of each afternoon in the wrong day")
    parser.add_argument("--out", type=Path, required=True,
                        help="Where to write the snapshot, normally under data/snapshots/")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print what would be fetched without writing anything")
    arguments = parser.parse_args()

    start = parse_date(arguments.start, "--start")
    end = parse_date(arguments.end, "--end")
    if end < start:
        raise Failure("--end is before --start")

    query = urllib.parse.urlencode({
        "latitude": arguments.lat,
        "longitude": arguments.lon,
        "start_date": start.isoformat(),
        "end_date": end.isoformat(),
        "daily": ",".join(DAILY_VARIABLES),
        "timezone": arguments.timezone,
        "wind_speed_unit": "ms",
    })
    url = f"{ARCHIVE}?{query}"

    if arguments.dry_run:
        print(f"would fetch {url}")
        return 0

    payload = fetch_json(url)
    rows = rows_from(payload, start, end)
    add_uv(rows, arguments.lat, arguments.lon, start, end, arguments.timezone)
    digest = write_snapshot(rows, arguments.out)

    provenance = {
        "source": "Open-Meteo historical weather API (ERA5 reanalysis)",
        "url": ARCHIVE,
        "requested": {
            "longitude": arguments.lon,
            "latitude": arguments.lat,
            "start": start.isoformat(),
            "end": end.isoformat(),
            "timezone": arguments.timezone,
        },
        # Where the model actually sampled, which is not where you asked. A
        # reanalysis grid cell is tens of kilometres across, and the difference
        # between the farm and the cell is a thing a reader should be able to
        # see rather than assume away.
        "answered_for": {
            "longitude": payload.get("longitude"),
            "latitude": payload.get("latitude"),
            "elevation_m": payload.get("elevation"),
            "timezone": payload.get("timezone"),
            "utc_offset_seconds": payload.get("utc_offset_seconds"),
        },
        "measurement": "reanalysis, not observation",
        "sources_by_column": {
            "rainfall_mm, min_air_temperature_c, max_air_temperature_c, "
            "solar_radiation_mj_per_m2, wind_speed_m_per_s":
                "Open-Meteo historical archive (ERA5)",
            "uv_index": "Open-Meteo air quality API (CAMS), daily maximum - the archive carries "
                        "no ultraviolet",
        },
        "wind_height_m": 10,
        "wind_statistic": "daily maximum, not daily mean",
        "days": len(rows),
        "licence": LICENCE,
        "attribution": ATTRIBUTION,
        "fetched_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "sha256": digest,
    }
    provenance_path = arguments.out.with_suffix(arguments.out.suffix + ".provenance.json")
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")

    print(f"wrote {arguments.out} ({len(rows)} days)")
    print(f"  sha256 {digest}")
    print(f"  grid cell {payload.get('latitude')}, {payload.get('longitude')} "
          f"at {payload.get('elevation')} m")
    print(f"  provenance {provenance_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failure as failure:
        print(f"error: {failure}", file=sys.stderr)
        sys.exit(1)
