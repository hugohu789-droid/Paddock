# ADR 0002 — CMakePresets is the single source of build truth

- **Status:** accepted
- **Date:** 2026-08-16
- **Milestone:** M1

## Context

The project has three entry points: the command line, VS Code and Qt Creator.
Each has its own way of storing build configuration — CMake Tools kits, Qt
Creator kits, ad-hoc shell scripts. Once two of them disagree, "works on my
machine" becomes a real answer, and CI becomes a fourth configuration nobody
tests locally.

M1's acceptance criterion is explicit: the same preset must configure, build and
test from all three entry points with identical clang-tidy output.

## Decision

`CMakePresets.json` is committed and is the only place build configuration
lives.

- Every preset uses Ninja and sets `CMAKE_EXPORT_COMPILE_COMMANDS=ON`.
- Both editors use **clangd** against the generated `compile_commands.json`, so
  in-editor diagnostics are the T2 gate's diagnostics. VS Code's Microsoft
  IntelliSense engine is explicitly disabled to stop a second opinion appearing.
- Machine-local settings — vcpkg root, compiler paths, scratch build dirs — go
  in `CMakeUserPresets.json`, which is gitignored.
- The vcpkg toolchain is adopted from `VCPKG_ROOT` by `cmake/PaddockVcpkg.cmake`
  before `project()`, so no preset has to name a path that exists on one machine.
- CI runs the same presets. The Windows job differs only in that it invokes them
  from a Developer Command Prompt so that Ninja can find `cl.exe`.

## Consequences

- A build problem reproduces everywhere or nowhere.
- Adding a build option means editing one file, not three.
- Contributors must have Ninja installed even if their editor bundles a
  different generator.
- `CMakeUserPresets.json` being gitignored means a broken machine-local preset
  cannot be diagnosed from the repository alone; `docs/setup.md` documents the
  shape of the file to compensate.
