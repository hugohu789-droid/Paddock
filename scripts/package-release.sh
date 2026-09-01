#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Gejile Hu. All rights reserved.
#
# Assemble a release: the desktop application, the data it can run without
# fetching anything, and the attributions that data carries.
#
# The acceptance test this exists to satisfy is a person, not a checksum: a
# stranger downloads the archive, opens the application and watches a year of
# weather-driven farm life. Everything below follows from that.
#
# **Where the runtime comes from is read out of the build, never guessed.** Qt,
# VTK and the vcpkg tree all record their locations in the build directory's
# CMakeCache.txt, so this script works on a developer machine and on a CI runner
# without either one hardcoding the other's paths.
#
# **Snapshots are never packaged**, and the reason splits in two - which is
# worth stating precisely, because getting it wrong in either direction is a
# mistake. What is in data/snapshots/ today is LINZ LiDAR and cadastre, and
# that is CC BY 4.0: it *may* lawfully travel, with attribution. It is kept out
# because it is bulk and goes stale, and because a hash plus a fetch script
# reproduces it exactly - the rule the whole project runs on.
#
# But data/snapshots/ is also where a NIWA CliFlo extract or a Manaaki Whenua
# S-map layer lands, and those may **not** travel: NIWA's DataHub licence
# forbids passing the data to third parties, and S-map Online is CC BY-NC-ND
# 3.0 NZ. See docs/validation/verify.md item 7. So the exclusion is by
# directory rather than by inspecting each file's licence, because the
# directory is the drop zone for sources that differ and the strictest governs.
#
# A scenario whose ground is missing draws flat and says so - app/src/
# AttachElevation.hpp - and the smoke test below now exercises that path.
#
# Usage:
#   scripts/package-release.sh --build build/desktop --out dist [--version 0.1.0]

set -euo pipefail

build_dir="build/desktop"
out_dir="dist"
version=""

while [ $# -gt 0 ]; do
  case "$1" in
    --build)   build_dir="$2"; shift 2 ;;
    --out)     out_dir="$2";   shift 2 ;;
    --version) version="$2";   shift 2 ;;
    *) echo "package-release: unknown argument $1" >&2; exit 2 ;;
  esac
done

repo_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "${repo_root}"

cache="${build_dir}/CMakeCache.txt"
[ -f "${cache}" ] || { echo "package-release: no build at ${build_dir}" >&2; exit 1; }

# The version is asked of the binary rather than read out of a header, so an
# archive can never claim a number the thing inside it would not print.
if [ -z "${version}" ]; then
  version="$("${build_dir}/bin/paddock" --version 2>/dev/null | tr -d '\r')"
fi
[ -n "${version}" ] || { echo "package-release: could not read the engine version" >&2; exit 1; }

cache_value() { grep -E "^$1:" "${cache}" | head -1 | cut -d= -f2- || true; }

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) platform="windows" ;;
  Darwin)               platform="macos"   ;;
  Linux)                platform="linux"   ;;
  *) echo "package-release: unsupported platform $(uname -s)" >&2; exit 1 ;;
esac

name="paddock-${version}-${platform}-x64"
stage="${out_dir}/${name}"
rm -rf "${stage}"
mkdir -p "${stage}/bin" "${stage}/data"

echo "package-release: staging ${name} from ${build_dir}"

# ---------------------------------------------------------------- binaries

exe_suffix=""
[ "${platform}" = "windows" ] && exe_suffix=".exe"
for tool in paddock-gui paddock; do
  src="${build_dir}/bin/${tool}${exe_suffix}"
  [ -f "${src}" ] || { echo "package-release: ${src} was not built" >&2; exit 1; }
  cp "${src}" "${stage}/bin/"
done

# ---------------------------------------------------------------- Qt, VTK, GDAL
#
# windeployqt and macdeployqt know about Qt and nothing else. VTK and the
# geospatial stack are this script's own business on every platform.

qt_core_dir="$(cache_value 'Qt6Core_DIR')"
vtk_dir="$(cache_value 'VTK_DIR')"
vcpkg_installed="$(cache_value 'VCPKG_INSTALLED_DIR')"
vcpkg_triplet="$(cache_value 'VCPKG_TARGET_TRIPLET')"

if [ "${platform}" = "windows" ]; then
  # .../lib/cmake/Qt6Core -> the Qt prefix three levels up.
  qt_prefix="$(cd "${qt_core_dir}/../../.." && pwd)"
  "${qt_prefix}/bin/windeployqt.exe" --release --no-translations \
    --no-system-d3d-compiler --no-opengl-sw "${stage}/bin/paddock-gui.exe"

  # .../lib/cmake/vtk-9.5 -> the VTK prefix three levels up.
  if [ -n "${vtk_dir}" ]; then
    vtk_prefix="$(cd "${vtk_dir}/../../.." && pwd)"
    cp "${vtk_prefix}"/bin/*.dll "${stage}/bin/" 2>/dev/null || true
  fi

  # GDAL, PROJ and their fan-out. **proj.db goes to share/proj, not bin.**
  # PROJ looks beside its own library first and then at ../share/proj; put the
  # database anywhere else and every coordinate transform fails at runtime,
  # which is how GisEnvironmentTest.ProjCanResolveNztm2000 first went red in CI.
  if [ -n "${vcpkg_installed}" ] && [ -n "${vcpkg_triplet}" ]; then
    vcpkg_root="${vcpkg_installed}/${vcpkg_triplet}"
    cp "${vcpkg_root}"/bin/*.dll "${stage}/bin/" 2>/dev/null || true
    if [ -d "${vcpkg_root}/share/proj" ]; then
      mkdir -p "${stage}/share/proj"
      cp "${vcpkg_root}"/share/proj/* "${stage}/share/proj/" 2>/dev/null || true
    fi
  fi

elif [ "${platform}" = "macos" ]; then
  qt_prefix="$(cd "${qt_core_dir}/../../.." && pwd)"
  "${qt_prefix}/bin/macdeployqt" "${stage}/bin/paddock-gui" || true

else
  # Linux ships against the distribution's own Qt6 and VTK rather than carrying
  # copies. Bundling them portably needs linuxdeploy and an AppImage, which is
  # a larger decision than this script should make on its own; until then the
  # tarball names its dependencies and the README repeats them.
  :
fi

# ---------------------------------------------------------------- data
#
# Everything a scenario needs that is not a fetched snapshot.
#
# **This was an allow-list and it silently shipped three releases short.** The
# list read `scenarios species pastures soils weather calibration farms`, which
# was every directory data/ held when it was written - and data/diseases has
# been missing from every release since M4, with data/economics and
# data/regulations missing since they were added. A released `paddock disease`
# had nothing to read.
#
# It is a deny-list now. Adding a directory to data/ ships it; leaving one out
# takes a deliberate line here, and the reason goes beside it. The property the
# allow-list was protecting - that nothing large or unlicensed leaves the
# machine by accident - is kept by naming what is excluded, by
# scripts/check-data-licences.sh refusing to let such a file into the
# repository at all, and by DataFilesTest.EveryDataDirectoryIsShippedOr
# ExplicitlyExcluded failing when a directory is neither shipped nor named
# here.
data_excluded="snapshots"

for path in data/*/; do
  dir="$(basename "${path}")"
  case " ${data_excluded} " in
    # Fetched LiDAR and cadastre. Gitignored for licence reasons, and the same
    # reasoning applies to an archive that leaves this machine. A scenario whose
    # ground is missing says so and draws flat - checked below rather than
    # assumed.
    *" ${dir} "*) continue ;;
  esac
  cp -r "${path}" "${stage}/data/"
done

# **The fetch scripts, because the NOTICE sends people to them.** Everything
# this archive does not carry - cadastre, climate from a source that will not
# let us pass it on, and the ground for a farm somebody defines themselves - is
# reachable only by running one of these, and until now they stayed in the
# repository while the instruction to run them went out in the box. Somebody
# who downloaded a release was told to run a file they did not have.
#
# The common case no longer needs them: `paddock ground fetch` downloads the
# tile a shipped scenario names, because the bundle pins its address and its
# hash. What is left for the scripts is the harder half - *searching* a
# catalogue for the tile that covers a farm nobody has defined yet - and the
# sources a user must fetch under their own licence.
#
# They are ours, GPL-3.0 like the rest, standard library only, and about 40 kB
# of text.
mkdir -p "${stage}/scripts"
cp scripts/*-snapshot.py scripts/winchmore-fetch.py "${stage}/scripts/"

cp LICENSE "${stage}/" 2>/dev/null || cp COPYING "${stage}/" 2>/dev/null || true
cp README.md "${stage}/"

# ---------------------------------------------------------------- attribution
#
# **Generated from the provenance files, never typed here.** Every snapshot this
# project ships carries a .provenance.json with the licensor's own wording;
# copying that wording by hand is how an attribution silently stops matching the
# data it is meant to cover.
notice="${stage}/NOTICE.txt"
{
  echo "Paddock ${version} - third-party data shipped with this release"
  echo
  echo "The simulator is GPL-3.0-or-later; see LICENSE. The data below travels"
  echo "under its own terms and is redistributed here with the attribution its"
  echo "licence requires."
  echo
  # **One entry per distinct credit, not one per file.** The same Open-Meteo
  # series is named by four scenarios, and a notice that repeated its
  # attribution four times read as a machine's output rather than as a credit
  # somebody is owed. Sorted so the order does not depend on directory walk
  # order, which would make two builds of the same tree differ.
  found=0
  while IFS= read -r line; do
    [ -n "${line}" ] || continue
    found=1
    printf '  [%s]
    %s

' "${line%%|*}" "${line#*|}"
  done < <(
    find "${stage}/data" -name '*.provenance.json' | sort | while IFS= read -r prov; do
      attribution="$(grep -oE '"attribution"[[:space:]]*:[[:space:]]*"[^"]*"' "${prov}" | cut -d'"' -f4)"
      licence="$(grep -oE '"licence"[[:space:]]*:[[:space:]]*"[^"]*"' "${prov}" | cut -d'"' -f4)"
      [ -n "${attribution}" ] || continue
      echo "${licence:-unstated}|${attribution}"
    done | sort -u
  )
  [ "${found}" -eq 1 ] || echo "  (no third-party data is shipped in this archive)"
  echo
  echo "Ground elevation and cadastre are NOT shipped. The scenarios that name"
  echo "them draw flat ground and say so, and everything else about the run is"
  echo "unchanged. To put a scenario on its measured surface:"
  echo
  echo "    bin/paddock ground fetch data/scenarios/lincoln-lurdf"
  echo
  echo "or press Fetch ground in the application, which appears when a farm has"
  echo "ground it has not got. Either way it downloads the one tile the scenario"
  echo "names and checks it against the hash the scenario pins before putting it"
  echo "anywhere. Elevation is Toitu Te Whenua LINZ open data under CC BY 4.0"
  echo "and needs no account."
  echo
  echo "Cadastre is the same licence but comes from the LINZ Data Service, which"
  echo "needs a free API key of your own; scripts/linz-snapshot.py says how to"
  echo "get one, and finding which tile covers a farm you define yourself is"
  echo "scripts/nz-elevation-snapshot.py. Both need Python."
  echo
  echo "Two sources are not fetchable on your behalf and never will be. NIWA"
  echo "CliFlo climate data is licensed to the person who registered for it and"
  echo "may not be passed on, and Manaaki Whenua S-map is CC BY-NC-ND. If you"
  echo "want either, you agree to their terms yourself and the data stays on"
  echo "your machine. The weather this release ships is Open-Meteo, CC BY 4.0,"
  echo "which carries no such restriction."
} > "${notice}"

# ---------------------------------------------------------------- prove it runs
#
# A package that was assembled is not a package that works. This renders a year
# headlessly from inside the staged tree, with the repository's own build
# directory out of the picture, so a missing runtime library fails here rather
# than in front of the person who downloaded it.
echo "package-release: checking the staged tree runs"
(
  cd "${stage}"
  ./bin/paddock${exe_suffix} scenario run data/scenarios/canterbury-grazed > /dev/null
) || { echo "package-release: the staged build does not run" >&2; exit 1; }

# **And that a scenario whose ground is missing still runs**, which is the state
# every archive is in and no other check here reaches.
#
# The comment above this used to claim `scenario run` proved it. It does not:
# that command builds a farmlet, a point model with no ground under it, so it
# would have passed with the terrain handling removed entirely. `dashboard` does
# take the ground, and for as long as it existed it exited 1 when the snapshot
# was absent - so this exact command failed in every release that shipped it,
# and the packaging test passed anyway.
echo "package-release: checking a scenario with no snapshot still runs"
(
  cd "${stage}"
  ./bin/paddock${exe_suffix} dashboard data/scenarios/lincoln-lurdf 2015 > /dev/null
) || { echo "package-release: a scenario whose LiDAR is absent does not run flat" >&2; exit 1; }

# ---------------------------------------------------------------- archive

mkdir -p "${out_dir}"
archive=""
if [ "${platform}" = "windows" ]; then
  archive="${name}.zip"
  (cd "${out_dir}" && powershell -NoProfile -Command \
    "Compress-Archive -Path '${name}' -DestinationPath '${archive}' -Force")
else
  archive="${name}.tar.gz"
  (cd "${out_dir}" && tar czf "${archive}" "${name}")
fi

echo "package-release: ${out_dir}/${archive}"
du -h "${out_dir}/${archive}" | cut -f1 | sed 's/^/package-release: size /'
