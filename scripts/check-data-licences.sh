#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Gejile Hu. All rights reserved.
#
# Nothing that may not be redistributed gets into the repository.
#
# Two of the sources this project uses forbid passing their data on: NIWA's
# DataHub licence, and Manaaki Whenua's S-map Online under CC BY-NC-ND. LINZ and
# Open-Meteo are CC BY 4.0 and could travel, but bulk downloads are kept out
# anyway - a scenario stays reproducible through a hash and a fetch script.
#
# A licence breach is not something to discover in a release. This checks the
# tree and the whole history, and is cheap enough to run on every pull request.

set -euo pipefail

fail=0

say_no() {
  echo "check-data-licences: $1" >&2
  fail=1
}

# 1. Snapshots are fetched, never committed - now or at any point in the past.
if git ls-files --error-unmatch 'data/snapshots/*' > /dev/null 2>&1; then
  say_no "a file under data/snapshots/ is tracked; snapshots are fetched, not committed"
fi
if [ -n "$(git log --all --diff-filter=A --format=%H -- 'data/snapshots/*' | head -1)" ]; then
  say_no "data/snapshots/ has been committed at some point in this history"
fi

# 2. The payload formats those sources come in, anywhere in the tree.
payloads=$(git ls-files -- '*.tif' '*.tiff' '*.geojson' '*.gpkg' '*.shp' '*.nc' '*.grib' || true)
if [ -n "${payloads}" ]; then
  say_no "geospatial or climate payloads are tracked:"
  echo "${payloads}" >&2
fi

# 3. A bulk dataset by size, whatever it is called. Screenshots are the only
#    large files this repository has a use for, so they are the exception and
#    they are named.
while IFS= read -r file; do
  [ -f "${file}" ] || continue
  case "${file}" in
    docs/images/*.png) continue ;;
  esac
  size=$(wc -c < "${file}")
  if [ "${size}" -gt 1000000 ]; then
    say_no "${file} is $((size / 1000)) kB; bulk data does not belong in the repository"
  fi
done < <(git ls-files)

if [ "${fail}" -eq 0 ]; then
  echo "check-data-licences: nothing redistributable-by-mistake is committed"
fi
exit "${fail}"
