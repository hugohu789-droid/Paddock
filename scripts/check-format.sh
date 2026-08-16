#!/usr/bin/env bash
# T1/T2 gate: every tracked C++ file matches the repository .clang-format.
# Usage: scripts/check-format.sh [file ...]   (no arguments = all tracked files)
set -euo pipefail

cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
if ! command -v "${CLANG_FORMAT}" >/dev/null 2>&1; then
  echo "check-format: ${CLANG_FORMAT} not found; see docs/setup.md" >&2
  exit 1
fi

if [ "$#" -gt 0 ]; then
  files=("$@")
else
  mapfile -t files < <(git ls-files '*.cpp' '*.hpp' '*.h' '*.cc')
fi

if [ "${#files[@]}" -eq 0 ]; then
  echo "check-format: nothing to check"
  exit 0
fi

failed=0
for file in "${files[@]}"; do
  [ -f "${file}" ] || continue
  if ! "${CLANG_FORMAT}" --style=file --dry-run --Werror "${file}" 2>&1; then
    failed=1
  fi
done

if [ "${failed}" -ne 0 ]; then
  echo >&2
  echo "check-format: run '${CLANG_FORMAT} -i --style=file <file>' to fix." >&2
  exit 1
fi

echo "check-format: ${#files[@]} file(s) clean"
