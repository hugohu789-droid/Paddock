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

# What this bound is for, and what it is not.
#
# It exists to catch a mirror that has stopped answering, which produces no
# output at all for as long as the job will allow. It is NOT a judgement about
# how slow an install may be, and sizing it as one was a mistake worth
# recording: 180 seconds was chosen from a single healthy measurement of the
# largest install, and the next slow-but-working run of that same install was
# killed three times over while it was still fetching packages one by one.
# Turning a slow install into a hard failure is worse than the hang, because it
# fails work that would have finished.
#
# So the default is deliberately loose - it only has to be shorter than the job
# timeout, so that a stall is reported and retried instead of running the clock
# out - and a job whose install is unusually large says so itself. The right
# fix is a stall detector that watches for no progress rather than for elapsed
# time, and that is recorded in docs/backlog.md rather than written here,
# because killing a process group under sudo is not something this repository
# can test anywhere but on CI.
#
# Measurements, so the next person tuning this has more than one: ninja-build
# with GDAL and PROJ installs in 20 seconds, and the map-view job's Qt6, VTK,
# clang and clang-tidy in 127 seconds on a fast mirror and more than 180 on a
# slow one. The clang-tidy job's ninja-build with clang-19 and clang-tidy-19
# has been measured at 19, 26 and 54 seconds across consecutive runs.
#
# **And one measurement of the failure, which is the useful one.** A run of
# that same clang-tidy install hit the 300s bound three times over, on Azure
# and then twice on archive.ubuntu.com. Against a 54 second worst case that is
# not a slow install, it is a stall - so the answer there was NOT to raise the
# bound. Raising it would repeat the earlier mistake in the other direction:
# 300 seconds is already six times the slowest healthy run, and a stall waited
# on for longer is still a stall. The job was re-run and passed. This is the
# case the stall detector in docs/backlog.md is for, and until it exists the
# right response to three timeouts on a job that normally takes under a minute
# is to re-run it, not to tune this number.
attempt_seconds="${CI_APT_TIMEOUT:-300}"

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
