# ADR 0006 — M1 CI covers three platforms; the GUI matrix waits for M3

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M1

## Context

T2 is specified as a three-platform matrix build *including the GUI*, plus full
core tests, conservation assertions, deterministic replay, clang-tidy, and a
Linux Debug run under ASan and UBSan.

In M1 there is no GUI. `app/`, `viz/` and `gis/` are skeletons that exist to
hold the dependency direction in the build graph. Building Qt6 and VTK through
vcpkg on three platforms costs tens of minutes per run — hours on a cold cache —
to compile a library that nothing yet calls. Paid on every pull request from the
first day, that cost is what makes teams start skipping their own gates.

## Decision

The T2 workflow ships now with everything that has something to check:

| Job | Covers |
|---|---|
| `hygiene` | clang-format, LF-only index, `core.ignorecase=false`, dependency direction, core determinism rules |
| `build` | ubuntu-24.04 (GCC), macos-14 (Apple Clang), windows-2022 (MSVC): configure, build, full test suite including conservation and deterministic replay |
| `sanitizers` | Linux Debug with ASan and UBSan, `halt_on_error` |
| `tidy` | clang-tidy 19 over the generated compile database, warnings as errors |

The full-GUI matrix — vcpkg binary cache, Qt6 and VTK, `windeployqt` /
`macdeployqt` smoke checks — is added in **M3**, when `viz/` and `app/` contain
the 2D and 3D views. M2's 2D map view will bring the first real Qt and VTK code
and the first version of that job.

Every non-negotiable principle already has a gate. Nothing that can be checked
today is deferred.

## Consequences

- Pull requests finish in minutes, so the gates stay honest.
- A Qt or VTK integration problem will surface in M2-M3 rather than now. That is
  acceptable: `viz/` and `app/` contain no logic to regress in the meantime.
- The CI matrix pins Ubuntu 24.04, macOS 14 and Windows 2022 explicitly rather
  than tracking `-latest`, so a runner image bump is a visible commit rather
  than a mysterious Tuesday failure.
- clang-format and clang-tidy are pinned to LLVM 19 from apt.llvm.org, matching
  the toolchain VS 2022 ships, so a developer on Windows and the Linux gate
  agree on what "formatted" means.
