#!/usr/bin/env bash
# Installs Debian packages on a CI runner, without hanging for half an hour.
#
# GitHub's Ubuntu images point apt at azure.archive.ubuntu.com, which
# intermittently stops responding rather than refusing. apt has no default
# timeout for that case, so a job doing nothing more than fetching a 129 kB
# ninja-build package can sit for thirty minutes and then be cancelled - which
# has happened here to the clang-tidy and ASan jobs.
#
# Four defences, cheapest first:
#   * an acquire timeout, so a dead mirror fails in seconds instead of never;
#   * a wall clock bound on each attempt, so a hang anywhere else becomes a
#     failure the retry can act on;
#   * retries, because the failure is intermittent;
#   * a fall back to the main archive.ubuntu.com from the second attempt.
#
# The wall clock bound is the one that was missing, and the first three were
# not enough without it: Acquire::Timeout covers the fetch and nothing else, so
# apt sitting on a repository handshake or a dpkg lock stalls forever, and a
# loop that only reacts to failure waits it out. Two jobs in one run were
# cancelled at their job timeouts on this step: the validation job at twenty
# minutes fetching ninja-build, and the GIS job at thirty fetching GDAL. Both
# take about twenty seconds when the mirror is answering.
#
# CI_APT_TIMEOUT overrides the per-attempt bound, in seconds.
#
# Usage: scripts/ci-apt-install.sh ninja-build libgdal-dev
set -euo pipefail

if [ "$#" -eq 0 ]; then
  echo "ci-apt-install: nothing to install" >&2
  exit 1
fi

export DEBIAN_FRONTEND=noninteractive


use_main_archive() {
  echo "ci-apt-install: falling back to archive.ubuntu.com"
  # Noble uses the deb822 format; older images still have sources.list.
  sudo sed -i 's|azure.archive.ubuntu.com|archive.ubuntu.com|g' \
    /etc/apt/sources.list.d/ubuntu.sources /etc/apt/sources.list 2>/dev/null || true
}

# Measured, not guessed. On a healthy runner, update and install together take
# 20 seconds for ninja-build with GDAL and PROJ, and 127 seconds for the
# map-view job's Qt6, VTK and clang - the largest install in the workflow. The
# bound covers the whole attempt rather than each apt-get call, so three
# attempts cost at most three times it: 9 minutes, which fits the tightest job
# budget in the workflow with the sleeps between attempts. 180 seconds is
# therefore generous against every install that is working and short against
# every one that is not.
attempt_seconds="${CI_APT_TIMEOUT:-180}"

attempts=3
for attempt in $(seq 1 "${attempts}"); do
  # From the second attempt, not the last. Azure is the mirror that stalls, so
  # trying it twice spends two timeouts learning the same thing.
  if [ "${attempt}" -gt 1 ]; then
    use_main_archive
  fi

  echo "ci-apt-install: attempt ${attempt} of ${attempts} (${attempt_seconds}s limit): $*"
  started=${SECONDS}
  # timeout runs as root rather than under it, so that when it has to kill
  # something it is killing its own child and not asking sudo to pass the
  # signal on. -k gives apt ten seconds to unwind before SIGKILL, because a
  # half-written dpkg state is worse than a slow one.
  # The acquire timeouts fail a stalled fetch in seconds rather than never, and
  # are written here rather than passed in: sudo resets the environment unless
  # sudoers allows otherwise, and a variable that silently arrives empty would
  # take these with it and leave apt waiting forever again.
  if sudo timeout -k 10 "${attempt_seconds}" bash -c '
       set -e
       opts="-o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15 -o Acquire::Retries=2"
       # Unquoted on purpose: these are several options, not one.
       # shellcheck disable=SC2086
       apt-get ${opts} update
       # shellcheck disable=SC2086
       apt-get ${opts} install -y --no-install-recommends "$@"
     ' _ "$@"; then
    echo "ci-apt-install: installed $* in $((SECONDS - started))s"
    exit 0
  else
    # Captured here rather than after the fi: an if whose condition is false
    # and which has no else returns 0 itself, so $? outside would always say
    # the attempt succeeded.
    #
    # 124 is what timeout(1) reports when it had to kill the command, which is
    # the case worth naming: it means apt was still going, not that it refused.
    status=$?
  fi
  if [ "${status}" -eq 124 ]; then
    echo "ci-apt-install: attempt ${attempt} hit the ${attempt_seconds}s limit" >&2
  else
    echo "ci-apt-install: attempt ${attempt} failed with status ${status}" >&2
  fi
  sleep $((attempt * 5))
done

echo "ci-apt-install: giving up after ${attempts} attempts" >&2
exit 1
