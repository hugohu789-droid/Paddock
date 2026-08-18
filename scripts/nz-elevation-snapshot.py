#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Gejile Hu. All rights reserved.

"""Fetch a LINZ elevation tile for one farm and write a hashed snapshot.

The sibling of scripts/linz-snapshot.py, and deliberately not part of it,
because the two reach different services on different terms. Vectors come from
the LINZ Data Service, which needs an API key and puts it in the URL. Elevation
is published as open data on S3 with **no key at all** - which is worth saying
plainly, because a key was tried first and the LINZ Data Service refused it:
the export API and the layers API both answer `Invalid API key scope`, and WCS
is not offered. None of that stands between this data and anyone.

What it fetches is a DEM and not a DSM. A digital surface model has the trees,
the hedges and the woolshed roof on it; a farm model wants the ground the grass
grows on.

**Tiles are checked, not collections.** A collection's bounding box is the union
of its tiles and can have holes in it: the Canterbury 2018-2019 collection's box
contains Lincoln and not one of its 290 tiles does. Selecting on the box would
have produced a confident download of the wrong part of the country.

Snapshots land in data/snapshots/, which is gitignored. What gets committed is
this script and the SHA-256 a scenario bundle pins, so a collaborator can fetch
the same file and prove it is the same one.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
import urllib.error
import urllib.parse
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

CATALOGUE = "https://nz-elevation.s3-ap-southeast-2.amazonaws.com"

TIMEOUT_SECONDS = 300


class Failure(Exception):
    """An error with something the user can do about it."""


def fetch_json(url: str) -> dict:
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response:
            return json.loads(response.read().decode("utf-8"))
    except urllib.error.HTTPError as error:
        raise Failure(f"{url} returned {error.code} {error.reason}") from None
    except urllib.error.URLError as error:
        raise Failure(f"cannot reach {url}: {error.reason}") from None


def covers(bbox, lon: float, lat: float) -> bool:
    return bool(bbox) and bbox[0] <= lon <= bbox[2] and bbox[1] <= lat <= bbox[3]


def dem_collections() -> list[dict]:
    """Every 1 m DEM collection in the catalogue, in NZTM2000.

    The 2193 in the path is the projection the tiles are already written in, so
    nothing here has to reproject and nothing can get the axis order wrong.
    """
    root = fetch_json(f"{CATALOGUE}/catalog.json")
    return [
        link
        for link in root.get("links", [])
        if link.get("rel") == "child"
        and "/dem_1m/" in link.get("href", "")
        and "/2193/" in link.get("href", "")
    ]


def collection_url(href: str) -> str:
    return f"{CATALOGUE}/" + href.lstrip("./")


def tile_covering(collection_href: str, lon: float, lat: float):
    """The tile containing the point, or None. Checks tiles, not the box."""
    url = collection_url(collection_href)
    collection = fetch_json(url)
    base = url.rsplit("/", 1)[0]
    for link in collection.get("links", []):
        if link.get("rel") != "item":
            continue
        item = fetch_json(f"{base}/" + link["href"].lstrip("./"))
        if covers(item.get("bbox"), lon, lat):
            return collection, item, base
    return None


def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url: str, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".partial")
    try:
        with urllib.request.urlopen(url, timeout=TIMEOUT_SECONDS) as response, \
                partial.open("wb") as out:
            while True:
                block = response.read(1 << 20)
                if not block:
                    break
                out.write(block)
    except (urllib.error.HTTPError, urllib.error.URLError) as error:
        partial.unlink(missing_ok=True)
        raise Failure(f"cannot download {url}: {error}") from None
    # Only now replace any existing snapshot, so a failed fetch never leaves a
    # half-written file where a scenario expects a complete one.
    partial.replace(destination)


def describe(collection: dict, item: dict, tile_url: str, digest: str) -> dict:
    properties = item.get("properties", {})
    licensors = [
        provider.get("name")
        for provider in collection.get("providers", [])
        if "licensor" in (provider.get("roles") or [])
    ]
    return {
        "collection": collection.get("title"),
        "tile": item.get("id"),
        "url": tile_url,
        # When the ground was actually flown, which is not when the file was
        # written and is the date a reader of a result wants.
        "captured_from": properties.get("start_datetime"),
        "captured_to": properties.get("end_datetime"),
        "published": properties.get("created"),
        "bbox_wgs84": item.get("bbox"),
        "projection": "EPSG:2193",
        "resolution_m": 1.0,
        "model": "DEM (bare earth), not DSM",
        "licence": collection.get("license"),
        "licensors": licensors,
        "attribution": (
            f"{collection.get('title')}. Sourced from LINZ elevation open data "
            f"({CATALOGUE}) and licensed for re-use under {collection.get('license')}."
        ),
        "fetched_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
        "sha256": digest,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fetch a LINZ 1 m DEM tile covering one point, and hash it.")
    parser.add_argument("--lon", type=float, required=True,
                        help="Longitude of a point on the farm, degrees east")
    parser.add_argument("--lat", type=float, required=True,
                        help="Latitude of a point on the farm, degrees north (negative in NZ)")
    parser.add_argument("--collection",
                        help="Collection path to take the tile from, for example "
                             "canterbury/selwyn_2023. Required unless --list is given, so that "
                             "which survey a farm rests on is a recorded choice rather than "
                             "whichever the catalogue happened to list first")
    parser.add_argument("--list", action="store_true",
                        help="List the collections that actually have a tile over the point, and "
                             "stop")
    parser.add_argument("--out", type=Path,
                        help="Where to write the tile, normally under data/snapshots/")
    parser.add_argument("--dry-run", action="store_true",
                        help="Find the tile and print what would be fetched, without fetching it")
    arguments = parser.parse_args()

    if arguments.list:
        print(f"collections with a tile over {arguments.lon}, {arguments.lat}:")
        for link in dem_collections():
            found = tile_covering(link["href"], arguments.lon, arguments.lat)
            if found is not None:
                _, item, _ = found
                print(f"  {link['href'].split('/')[1]:<28} {link.get('title')}  [{item['id']}]")
        return 0

    if not arguments.collection:
        raise Failure("--collection is required; run with --list to see which ones have a tile "
                      "over this point")
    if not arguments.out and not arguments.dry_run:
        raise Failure("--out is required unless --dry-run is given")

    href = f"./{arguments.collection.strip('/')}/dem_1m/2193/collection.json"
    found = tile_covering(href, arguments.lon, arguments.lat)
    if found is None:
        raise Failure(
            f"no tile in {arguments.collection} covers {arguments.lon}, {arguments.lat}. "
            "A collection's bounding box can contain a point that none of its tiles do; "
            "run with --list to see which collections actually have one.")

    collection, item, base = found
    asset = item.get("assets", {}).get("visual", {})
    tile_url = f"{base}/" + str(asset.get("href", "")).lstrip("./")

    print(f"collection  {collection.get('title')}")
    print(f"tile        {item.get('id')}")
    print(f"captured    {item.get('properties', {}).get('start_datetime')} to "
          f"{item.get('properties', {}).get('end_datetime')}")
    print(f"licence     {collection.get('license')}")
    print(f"url         {tile_url}")

    if arguments.dry_run:
        print("dry run: nothing fetched")
        return 0

    download(tile_url, arguments.out)
    digest = sha256_of(arguments.out)

    provenance = describe(collection, item, tile_url, digest)
    provenance_path = arguments.out.with_suffix(arguments.out.suffix + ".provenance.json")
    provenance_path.write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")

    print(f"wrote       {arguments.out} ({arguments.out.stat().st_size} bytes)")
    print(f"sha256      {digest}")
    print(f"            {provenance_path}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Failure as failure:
        print(f"error: {failure}", file=sys.stderr)
        sys.exit(1)
