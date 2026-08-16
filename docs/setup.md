# Setup

Paddock builds from one place: `CMakePresets.json`. The command line, VS Code
and Qt Creator all consume it, and no editor keeps its own build configuration.
If a build works in one of them it works in all three, and it works in CI.

## Prerequisites

| Tool | Minimum | Notes |
|---|---|---|
| CMake | 3.25 | Presets version 6 |
| Ninja | any | Every preset uses it |
| C++17 compiler | GCC 12, Clang 15, MSVC 19.3 | Tested on GCC 13, Apple Clang 15, MSVC 19.44 |
| clang-format / clang-tidy | 19 | Only for the gates; CI pins 19 |
| vcpkg | optional | Not needed for `core/` or the tests |

`core/` has no external dependencies at all. gtest and toml++ (used by
`config/`) come from vcpkg when a manifest install is in play and are fetched at
pinned commits otherwise, so a clone plus a compiler plus network access is a
complete environment.

## Command line

```bash
git clone https://github.com/hugohu789-droid/Paddock.git
cd Paddock
scripts/install-hooks.sh          # pre-commit gate, LF index, case sensitivity
cmake --preset default
cmake --build --preset default
ctest --preset default
```

`ctest --preset fast` runs the pre-commit subset: everything labelled `fast`,
which is every test except the statistical shape suites. The presets run tests
in parallel, so the whole suite takes a couple of seconds and there is no reason
to run less than all of it locally.

On **Windows**, run those commands from a *Developer Command Prompt for VS 2022*
(or a shell where `vcvars64.bat` has been sourced) so that Ninja can find
`cl.exe`. Nothing else differs; the presets are the same. If Visual Studio's
bundled vcpkg is present, `VCPKG_ROOT` is already set inside that prompt and the
manifest's `tests` feature installs gtest automatically.

### Machine-local settings

`CMakeUserPresets.json` is gitignored and is the right place for anything
specific to your machine — a vcpkg root, a compiler path, a scratch build
directory. It must never be committed, and `CMakePresets.json` must never
contain a path that only exists on one machine.

```json
{
  "version": 6,
  "configurePresets": [
    { "name": "local", "inherits": "default",
      "cacheVariables": { "PADDOCK_WARNINGS_AS_ERRORS": "OFF" } }
  ]
}
```

## VS Code

Install the recommended extensions (VS Code offers them from
`.vscode/extensions.json`): **clangd**, **CMake Tools**, **EditorConfig**.

`.vscode/settings.json` is committed and already:

- forces `cmake.useCMakePresets`, so the preset list is the same as the CLI's;
- points clangd at `build/default/compile_commands.json`;
- turns on `--clang-tidy`, so clangd reports the repository `.clang-tidy` rules
  in the editor — the same findings the T2 gate reports;
- enables format-on-save through clangd, using the repository `.clang-format`;
- disables the Microsoft C/C++ IntelliSense engine, which would otherwise fight
  clangd and produce a second, different set of diagnostics.

Configure once with **CMake: Select Configure Preset → default**, then build and
test from the status bar. Diagnostics appear only after the first configure,
because they come from the generated compile database.

## Qt Creator

1. **File → Open File or Project…** and select `CMakeLists.txt`.
2. Qt Creator reads `CMakePresets.json` and offers the presets as import
   candidates. Select **default** (and **release** if you want both). Do not
   create a hand-made kit configuration: the preset carries the generator, the
   build type, `CMAKE_EXPORT_COMPILE_COMMANDS` and the vcpkg toolchain.
3. **Edit → Preferences → C++ → Code Model**: enable **clangd** and point it at
   the build directory's `compile_commands.json`
   (`<repo>/build/default/compile_commands.json`). This is what makes Qt
   Creator's diagnostics identical to VS Code's and to CI's.
4. **Edit → Preferences → Beautifier → ClangFormat**: set the ClangFormat
   command to your clang-format 19 binary, choose *Use file .clang-format
   defined by project*, and enable **Format on file save**.

## The gates

| Gate | When | Command |
|---|---|---|
| T0 | on save | Format-on-save and clangd diagnostics in both editors |
| T1 | pre-commit | `.githooks/pre-commit` — format, line endings, `ctest --preset fast` |
| T2 | every PR | `.github/workflows/ci.yml` |

Run any gate by hand:

```bash
scripts/check-format.sh
scripts/check-line-endings.sh
scripts/check-dependency-direction.sh
ctest --preset fast
```

If your clang-format major differs from CI's, point the script at the right
binary: `CLANG_FORMAT=clang-format-19 scripts/check-format.sh`.

## Line endings and case sensitivity

The index stores LF for every text file; working trees may use whatever the
platform prefers. `scripts/install-hooks.sh` also sets `core.ignorecase=false`,
so a file renamed only in case on Windows is a real rename to Linux and macOS
too. The T1 and T2 gates check both.

## Data

Bulk datasets are never committed. `data/` holds definitions and calibration
tables; downloaded LINZ, NIWA and Manaaki Whenua snapshots live in
`data/snapshots/`, which is gitignored — fetch scripts and content hashes are
committed instead, so a scenario bundle can be reproduced from them.
