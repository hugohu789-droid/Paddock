#!/usr/bin/env bash
# T1/T2 gate: the index stores LF for every text file, whatever the working tree
# uses. Together with core.ignorecase=false this keeps a Windows checkout from
# quietly changing what Linux and macOS see.
set -euo pipefail

cd "$(dirname "$0")/.."

offenders=$(git ls-files --eol | awk '$1 == "i/crlf" || $1 == "i/mixed" { print }')

if [ -n "${offenders}" ]; then
  echo "check-line-endings: these files are stored with CRLF in the index:" >&2
  echo "${offenders}" >&2
  echo >&2
  echo "Fix with: git add --renormalize . && git commit" >&2
  exit 1
fi

# **And no byte-order mark, which on a shell script is fatal.**
#
# Windows PowerShell writes one whenever it is asked for `-Encoding utf8`, so
# any edit to a committed file that goes through Set-Content or Out-File adds
# three bytes to the front of it. Most formats do not care - CMake ignores one,
# and one has been sitting in tests/CMakeLists.txt for several commits without
# hurting anything.
#
# A shell script cares completely. The kernel reads the first line looking for
# `#!`, finds `ï»¿#!` instead, and the file will not execute at all:
# "ï»¿#!/usr/bin/env: No such file or directory". That is exactly how
# scripts/package-release.sh was committed broken - it was edited from
# PowerShell, every test still passed because nothing runs the packaging script,
# and it was only caught by running it.
#
# Checked for every text file rather than only scripts, because the cost is one
# `head -c 3` each and the alternative is deciding, per format, whether a
# stray mark is harmless this time.
marked=""
while IFS= read -r file; do
  [ -f "${file}" ] || continue
  case "$(head -c 3 "${file}" | od -An -tx1 | tr -d ' ')" in
    efbbbf*) marked="${marked}${file}"$'
' ;;
  esac
done < <(git ls-files -- '*.sh' '*.py' '*.cpp' '*.hpp' '*.txt' '*.toml' '*.csv' '*.md' '*.json' '*.cmake')

if [ -n "${marked}" ]; then
  echo "check-line-endings: these files begin with a UTF-8 byte-order mark:" >&2
  echo "${marked}" >&2
  echo "A shell script with one will not execute at all. PowerShell's -Encoding utf8" >&2
  echo "writes them; use -Encoding utf8NoBOM, or edit the file with something else." >&2
  exit 1
fi

# Unset means a case-sensitive filesystem, which is what the setting asks for.
if [ "$(git config --default false --get core.ignorecase)" != "false" ]; then
  echo "check-line-endings: core.ignorecase is not false." >&2
  echo "Fix with: scripts/install-hooks.sh" >&2
  exit 1
fi

echo "check-line-endings: index is LF-only, no byte-order marks, core.ignorecase=false"
