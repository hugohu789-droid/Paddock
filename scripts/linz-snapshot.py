#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Gejile Hu. All rights reserved.

"""Fetch a LINZ Data Service layer for one farm and write a hashed snapshot.

This is the "live" adapter of the three CLAUDE.md asks for, and it is a script
rather than a class on purpose: no simulation run may reach the network, or the
same scenario bundle would produce different results on different days and a
golden baseline would mean nothing. See docs/adr/0012-linz-sources.md.

The key comes from the LINZ_API_KEY environment variable and from nowhere else.
Not a command-line argument - arguments are visible to every other process on
the machine and land in shell history - and never printed, not in the log line,
not in an error, not in the provenance written beside the snapshot. LINZ says a
key "allows you to access your LDS account without you having to provide your
password", and its guidance describes no way to restrict one, so a leaked key is
a leaked account.

Snapshots land in data/snapshots/, which is gitignored. What gets committed is
this script and the SHA-256 the scenario bundle pins, so a collaborator with an
account of their own can reproduce the file and prove it is the same one.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

WFS_ENDPOINT = "https://data.linz.govt.nz/services;key={key}/wfs"
NZTM2000 = "EPSG:2193"

# Attribution is a condition of the licence, not a courtesy: LINZ data is
# CC BY 4.0. The layer URL is filled in per fetch.
ATTRIBUTION = (
    "Sourced from the LINZ Data Service ({layer_url}) and licensed by "
    "Land Information New Zealand for re-use under the Creative Commons "
    "Attribution 4.0 International licence."
)


class Failure(Exception):
    """An error with something the user can do about it."""


def read_api_key() -> str:
    key = os.environ.get("LINZ_API_KEY", "").strip()
    if not key:
        raise Failure(
            "LINZ_API_KEY is not set.\n"
            "\n"
            "Create a key at https://data.linz.govt.nz (log in, click your name, "
            "then 'API Keys'), and put it in the environment:\n"
            "\n"
            "    export LINZ_API_KEY=...        # bash\n"
            "    setx LINZ_API_KEY ...          # Windows, then open a new terminal\n"
            "\n"
            "Do not pass it as an argument to this script and do not commit it: a LINZ "
            "key stands in for your account password."
        )
    return key


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def build_request(key: str, layer: str, extent: tuple[float, float, float, float],
                  fmt: str) -> str:
    west, south, east, north = extent
    query = {
        "service": "WFS",
        "version": "2.0.0",
        "request": "GetFeature",
        "typeNames": layer,
        "outputFormat": fmt,
        "srsName": NZTM2000,
        # WFS 2.0 takes the bounding box as min,min,max,max followed by the CRS
        # the numbers are in - without that last part the server is entitled to
        # read them in the axis order EPSG:2193 declares, which is northing
        # first, and return the wrong part of the country.
        "bbox": f"{west},{south},{east},{north},{NZTM2000}",
    }
    return WFS_ENDPOINT.format(key=key) + "?" + urllib.parse.urlencode(query)


def redact(text: str, key: str) -> str:
    """Removes the key from anything about to be printed."""
    return text.replace(key, "<LINZ_API_KEY>") if key else text


def explain(error: urllib.error.HTTPError) -> str:
    """The server's own account of what was wrong with the request, if it gave one."""
    try:
        body = error.read().decode("utf-8", errors="replace").strip()
    except OSError:
        return ""
    if not body:
        return ""
    # The report is XML wrapped over several lines; a report is easier to read
    # collapsed, and only the first part of it says anything.
    collapsed = " ".join(body.split())
    return "\n" + (collapsed[:600] + "..." if len(collapsed) > 600 else collapsed)


def fetch(url: str, destination: Path, key: str) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    try:
        with urllib.request.urlopen(url, timeout=300) as response, partial.open("wb") as out:
            while True:
                block = response.read(1 << 20)
                if not block:
                    break
                out.write(block)
    except urllib.error.HTTPError as error:
        partial.unlink(missing_ok=True)
        if error.code in (401, 403):
            raise Failure(
                f"LINZ refused the request ({error.code}). The key in LINZ_API_KEY is "
                "wrong, expired, or lacks access to this layer."
            ) from None
        # WFS says what it did not like in the body, as an ows:ExceptionReport.
        # Without it a 400 is unactionable - the layer name, the output format
        # and the bounding box all fail the same way. Redacted before printing
        # because the endpoint carries the key and servers echo the URL back.
        raise Failure(
            redact(f"LINZ returned {error.code} {error.reason}.{explain(error)}", key)
        ) from None
    except urllib.error.URLError as error:
        partial.unlink(missing_ok=True)
        raise Failure(redact(f"Cannot reach LINZ: {error.reason}", key)) from None

    # Only now replace any existing snapshot, so a failed fetch never leaves a
    # half-written file where a scenario expects a complete one.
    partial.replace(destination)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fetch a LINZ layer for one farm and write a hashed snapshot.")
    parser.add_argument("--layer", required=True,
                        help="LINZ layer identifier, for example layer-50772 for NZ Primary "
                             "Parcels")
    parser.add_argument("--extent", required=True, nargs=4, type=float,
                        metavar=("WEST", "SOUTH", "EAST", "NORTH"),
                        help="Farm extent in NZTM2000 metres")
    parser.add_argument("--out", required=True, type=Path,
                        help="Where to write the snapshot, normally under data/snapshots/")
    # GeoPackage would be the natural choice and is not on offer: LINZ serves it
    # from the export API, not from WFS, whose GetCapabilities lists only GML,
    # KML, CSV and JSON. GeoJSON is the one of those that keeps geometry and
    # attributes together, and being text it diffs and hashes stably. Shapefile
    # is forbidden by CLAUDE.md and is not used here either way.
    parser.add_argument("--format", default="application/json",
                        help="WFS outputFormat; GeoJSON by default, which is the only vector "
                             "format LINZ WFS offers that CLAUDE.md permits")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print what would be requested, with the key redacted, and stop")
    arguments = parser.parse_args()

    key = read_api_key()
    url = build_request(key, arguments.layer, tuple(arguments.extent), arguments.format)

    print(f"layer   {arguments.layer}")
    print("extent  {:.1f} {:.1f} {:.1f} {:.1f} (NZTM2000 metres)".format(*arguments.extent))
    print(f"request {redact(url, key)}")

    if arguments.dry_run:
        print("dry run: nothing fetched")
        return 0

    fetch(url, arguments.out, key)
    digest = sha256_of(arguments.out)

    provenance = {
        "layer": arguments.layer,
        "extent_nztm2000": arguments.extent,
        "format": arguments.format,
        "fetched_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "sha256": digest,
        "attribution": ATTRIBUTION.format(
            layer_url=f"https://data.linz.govt.nz/{arguments.layer}/"),
        # Deliberately absent: anything derived from the key. This file is meant
        # to be committed or pasted into a scenario bundle.
    }
    provenance_path = arguments.out.with_suffix(arguments.out.suffix + ".provenance.json")
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")

    print(f"wrote   {arguments.out} ({arguments.out.stat().st_size} bytes)")
    print(f"sha256  {digest}")
    print(f"        {provenance_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failure as failure:
        print(f"error: {failure}", file=sys.stderr)
        sys.exit(1)
