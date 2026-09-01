#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Gejile Hu. All rights reserved.
"""Fetch the Winchmore irrigation trial's pasture production data and rebuild
`data/calibration/winchmore-annual-production.csv` from it.

**The script is committed and the workbook is not**, which is this project's
rule for anything it did not write: a fetch script plus a hash reproduces the
input, where a committed copy of somebody else's dataset goes stale and takes
their licence with it. The derived CSV is committed because it is 25 rows.

The dataset is CC BY 4.0. Its ReadMeFirst strongly encourages users to send a
Data Use form to winchmore@agresearch.co.nz saying who they are and what they
are using it for. **This script does not do that for you** - it is a courtesy
addressed to a person, and it is yours to extend.

    python scripts/winchmore-fetch.py [--out data/calibration/...csv]

Needs only the standard library: an xlsx is a zip of XML, and reading it that
way keeps this runnable on a machine with nothing installed, which is the same
reason core/ has no dependencies.
"""

import argparse
import hashlib
import statistics
import sys
import urllib.request
import zipfile
from pathlib import Path
from xml.etree import ElementTree

# The file on Figshare, and what it should hash to. A changed hash means the
# dataset was updated - which the repository says happens "on an ad hoc basis" -
# and the right response is to look at what changed rather than to trust it.
FILE_URL = "https://ndownloader.figshare.com/files/25971053"
FILE_SHA256 = "76ed719f1171a5309bc85285c9ab997c48e2121d7d5c620d0fdace1daecab00b"
DOI = "10.6084/m9.figshare.13530209"

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"

# Where each treatment's block starts in the "Monthly, seasonal, yearly prod"
# sheet. Found by reading the block titles, which end "... 10% treatment.",
# "... dryland treatment." and so on.
TREATMENTS = {
    "irrigated_10pc": 0,
    "irrigated_15pc": 32,
    "irrigated_20pc": 63,
    "dryland": 127,
}


def read_sheet(archive, path):
    """One worksheet as a list of rows of strings."""
    shared = []
    for item in ElementTree.fromstring(archive.read("xl/sharedStrings.xml")).iter(NS + "si"):
        shared.append("".join(t.text or "" for t in item.iter(NS + "t")))

    rows = []
    for row in ElementTree.fromstring(archive.read(path)).iter(NS + "row"):
        cells = []
        for cell in row.iter(NS + "c"):
            value = cell.find(NS + "v")
            if value is None:
                cells.append("")
            elif cell.get("t") == "s":
                cells.append(shared[int(value.text)])
            else:
                cells.append(value.text or "")
        rows.append(cells)
    return rows


def annual_totals(rows, start):
    """A treatment's year and annual total, out of the block beginning at `start`."""
    header = next(i for i in range(start, start + 5) if rows[i] and "TOT" in rows[i])
    total_column = rows[header].index("TOT")

    out = {}
    for row in rows[header + 1:]:
        if not row or not row[0] or row[0].startswith("Monthly"):
            break
        # The sheet carries its own MEAN, MIN and MAX rows. They are recomputed
        # here rather than read, so a summary that disagrees with its own series
        # would show up instead of being copied.
        if row[0] in ("MEAN", "MIN", "MAX") or len(row) <= total_column or not row[total_column]:
            continue
        try:
            out[row[0]] = float(row[total_column])
        except ValueError:
            continue
    return out


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path,
                        default=Path("data/calibration/winchmore-annual-production.csv"))
    parser.add_argument("--keep", type=Path, default=None,
                        help="also save the downloaded workbook here")
    args = parser.parse_args()

    print(f"fetching {FILE_URL}")
    with urllib.request.urlopen(FILE_URL, timeout=120) as response:  # noqa: S310
        blob = response.read()

    digest = hashlib.sha256(blob).hexdigest()
    if digest != FILE_SHA256:
        print(f"sha256 {digest}\nexpected {FILE_SHA256}\n\n"
              "The dataset has changed since this script was written. AgResearch update it "
              "on an ad hoc basis; read what changed before trusting the new figures, then "
              "update FILE_SHA256.", file=sys.stderr)
        return 1
    print(f"sha256 {digest} - matches")

    if args.keep is not None:
        args.keep.write_bytes(blob)

    import io
    archive = zipfile.ZipFile(io.BytesIO(blob))
    rows = read_sheet(archive, "xl/worksheets/sheet3.xml")

    series = {name: annual_totals(rows, start) for name, start in TREATMENTS.items()}
    years = sorted(series["dryland"])

    dryland = [series["dryland"][year] for year in years]
    print(f"\ndryland  n={len(dryland)}  mean={statistics.mean(dryland):.0f}  "
          f"min={min(dryland):.0f}  max={max(dryland):.0f}")
    for name in ("irrigated_10pc", "irrigated_15pc", "irrigated_20pc"):
        values = [series[name][year] for year in years if year in series[name]]
        print(f"{name:<16} mean={statistics.mean(values):.0f}  "
              f"ratio to dryland {statistics.mean(values) / statistics.mean(dryland):.2f}")

    # The header the committed CSV carries is prose about provenance, so it is
    # kept rather than regenerated: this script rewrites the rows under whatever
    # header is already there.
    existing = args.out.read_text(encoding="utf-8").splitlines()
    header_lines = []
    for line in existing:
        header_lines.append(line)
        if line.startswith("year,"):
            break

    body = []
    for year in years:
        body.append(",".join(
            [year] + [f"{series[name][year]:.0f}" if year in series[name] else ""
                      for name in ("dryland", "irrigated_10pc", "irrigated_15pc",
                                   "irrigated_20pc")]))

    args.out.write_text("\n".join(header_lines + body) + "\n", encoding="utf-8", newline="")
    print(f"\nwrote {args.out} ({len(body)} years)")
    print(f"cite: Winchmore Database, AgResearch, doi:{DOI}, CC BY 4.0")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
