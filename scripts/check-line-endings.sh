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

# Unset means a case-sensitive filesystem, which is what the setting asks for.
if [ "$(git config --default false --get core.ignorecase)" != "false" ]; then
  echo "check-line-endings: core.ignorecase is not false." >&2
  echo "Fix with: scripts/install-hooks.sh" >&2
  exit 1
fi

echo "check-line-endings: index is LF-only, core.ignorecase=false"
