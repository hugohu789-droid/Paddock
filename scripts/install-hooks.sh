#!/usr/bin/env bash
# Points git at the tracked hooks and sets the repository-local git settings
# that the T1 gate assumes. Run once after cloning; see docs/setup.md.
set -euo pipefail

cd "$(dirname "$0")/.."

git config core.hooksPath .githooks
git config core.ignorecase false
git config core.autocrlf false

chmod +x .githooks/* scripts/*.sh 2>/dev/null || true

echo "Hooks installed:"
echo "  core.hooksPath   = $(git config core.hooksPath)"
echo "  core.ignorecase  = $(git config core.ignorecase)"
echo "  core.autocrlf    = $(git config core.autocrlf)"
