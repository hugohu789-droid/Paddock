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
# **Snapshots are never packaged.** data/snapshots/ holds fetched LiDAR and
# cadastre; it is gitignored for licence reasons and the same reasoning applies
# to an archive that leaves this machine. A scenario whose ground is missing
# says so and draws flat - which is checked below rather than assumed.
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
# Everything a scenario needs that is not a fetched snapshot. The exclusion is
# explicit rather than a pattern, so adding a directory to data/ cannot smuggle
# a payload into a release by accident.
for dir in scenarios species pastures soils weather calibration farms; do
  [ -d "data/${dir}" ] && cp -r "data/${dir}" "${stage}/data/"
done
rm -rf "${stage}/data/snapshots"

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
  found=0
  while IFS= read -r prov; do
    attribution="$(grep -oE '"attribution"[[:space:]]*:[[:space:]]*"[^"]*"' "${prov}" | cut -d'"' -f4)"
    licence="$(grep -oE '"licence"[[:space:]]*:[[:space:]]*"[^"]*"' "${prov}" | cut -d'"' -f4)"
    [ -n "${attribution}" ] || continue
    found=1
    echo "  $(basename "${prov%.provenance.json}")  [${licence}]"
    echo "    ${attribution}"
    echo
  done < <(find "${stage}/data" -name '*.provenance.json' | sort)
  [ "${found}" -eq 1 ] || echo "  (no third-party data is shipped in this archive)"
  echo
  echo "Ground elevation and cadastre are NOT shipped. The scenarios that name"
  echo "them draw flat ground and say so. To run them on the measured surface,"
  echo "fetch the snapshots with the scripts named in each scenario file; they"
  echo "come from Toitu Te Whenua LINZ under CC BY 4.0."
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
