#!/usr/bin/env bash
# Installs Debian packages on a CI runner, without hanging for half an hour.
#
# GitHub's Ubuntu images point apt at azure.archive.ubuntu.com, which
# intermittently stops responding rather than refusing. apt has no default
# timeout for that case, so a job doing nothing more than fetching a 129 kB
# ninja-build package can sit for thirty minutes and then be cancelled - which
# has happened here to the clang-tidy and ASan jobs.
#
# Three defences, cheapest first:
#   * an acquire timeout, so a dead mirror fails in seconds instead of never;
#   * retries, because the failure is intermittent;
#   * a fall back to the main archive.ubuntu.com on the last attempt.
#
# Usage: scripts/ci-apt-install.sh ninja-build libgdal-dev
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "ci-apt-install: nothing to install" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive

# Fail a stalled fetch rather than waiting on it forever.
timeouts=(-o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15
          -o Acquire::Retries=2)

use_main_archive() {
  echo "ci-apt-install: falling back to archive.ubuntu.com"
  # Noble uses the deb822 format; older images still have sources.list.
  sudo sed -i 's|azure.archive.ubuntu.com|archive.ubuntu.com|g' \
    /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list 2>/dev/null || true
}

attempts=3
for attempt in $(seq 1 "${attempts}"); do
  if [ "${attempt}" -eq "${attempts}" ]; then
    use_main_archive
  fi

  echo "ci-apt-install: attempt ${attempt} of ${attempts}: $*"
  if sudo apt-get "${timeouts[@]}" update \
    && sudo apt-get "${timeouts[@]}" install -y --no-install-recommends "$@"; then
    echo "ci-apt-install: installed $*"
    exit 0
  fi

  echo "ci-apt-install: attempt ${attempt} failed" >&2
  sleep $((attempt * 5))
done

echo "ci-apt-install: giving up after ${attempts} attempts" >&2
exit 1
