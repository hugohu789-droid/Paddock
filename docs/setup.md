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

## The map view

The GUI is off by default. Building it needs Qt6 and a VTK **built against
Qt6**:

```bash
cmake --preset gui -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x/gcc_64;/path/to/vtk"
cmake --build --preset gui
./build/gui/bin/paddock-gui data/scenarios/canterbury-baseline
```

Two things worth knowing before you spend an afternoon on them:

- **Distribution packages of VTK's Qt integration are Qt5 until Ubuntu 26.04.**
  On 24.04 or Debian trixie, `libvtk9-qt-dev` links `qtbase5-dev` and cannot be
  used from a Qt6 application. Ubuntu 26.04 ships VTK 9.5.2 against
  `qt6-base-dev`; anywhere older, build VTK yourself or use vcpkg.
- **On MSVC, the build type has to match the VTK you link.** The `gui` preset is
  RelWithDebInfo because a Debug build (`/MDd`) linking a Release VTK (`/MD`)
  mixes C runtimes and dies inside the first widget's constructor with a stack
  check failure and no message. For a Debug GUI build, point
  `CMAKE_PREFIX_PATH` at a Debug VTK.

`paddock-gui <bundle> --smoke` renders one frame and exits, which is what CI
runs under `xvfb-run`.

## The geospatial stack

`gis/` is off by default too. Nothing in the pasture, weather or livestock work
needs it, and `cmake --preset default` has to keep succeeding on a machine that
has never heard of GDAL.

```bash
sudo apt-get install libgdal-dev libproj-dev   # Linux
brew install gdal proj                         # macOS
cmake --preset gis
cmake --build --preset gis
ctest --preset ci-gis -L gis
```

On Windows the same preset takes GDAL and PROJ from vcpkg, so it needs the
toolchain file. Installing them once with `vcpkg install gdal proj geos` and
then configuring against that install is far quicker than a manifest install,
which rebuilds into the build directory:

```bash
cmake --preset gis -DVCPKG_MANIFEST_MODE=OFF -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build/gis
ctest --test-dir build/gis -L gis
```

**This is the Windows gate for `gis/`.** The `gis` CI job runs Linux and macOS
only: on GitHub's runners the Windows build of GDAL from vcpkg source took
1h3m47s against Linux's 1m14s, and Actions caches are scoped per branch, so a
new branch pays it again. Windows geospatial coverage therefore comes from
running the three commands above before merging anything that touches `gis/`.
There is a `GIS Windows (vcpkg, on request)` job in the Actions tab for when
that is not enough. See [ADR 0011](adr/0011-gdal-and-proj.md).

Four things worth knowing before you spend an afternoon on them:

- **GDAL 3.0 and PROJ 6.0 are floors, and configuration fails below them.**
  Ubuntu 24.04 ships GDAL 3.8.4, Ubuntu 26.04 ships 3.12.2; anything older than
  a 2019 distribution will not configure. That is deliberate — see
  [ADR 0011](adr/0011-gdal-and-proj.md).
- **`proj.db` is the file that goes missing.** PROJ 6 keeps its coordinate
  operation tables in a SQLite database found through a search path baked in
  when PROJ was built. A PROJ that cannot find it still links and still runs,
  and fails every transform at the point of use.

  A vcpkg PROJ on Windows does this out of the box: `proj.db` is installed at
  `<vcpkg>/installed/x64-windows/share/proj/proj.db`, and the library looks in
  `%LOCALAPPDATA%\proj`, which does not exist. The symptom is six red tests and
  a message naming the search path:

  ```
  PROJ cannot resolve EPSG:2193. Its database search path is
  "C:\Users\you\AppData\Local/proj"; proj.db is missing from it, or PROJ_DATA
  points elsewhere.
  ```

  Configuring `gis` locates `proj.db` and prints where (`-- gis: proj.db at
  ...`), and the test suite sets `PROJ_DATA` from it, so `ctest -L gis` works
  without anyone exporting anything. Running the application by hand is not
  covered by that: export `PROJ_DATA` yourself, pointing at the directory
  holding `proj.db`.
- **EPSG:2193 declares its axes as (northing, easting).** GDAL 3 honours the
  authority's order, so it hands coordinates back the opposite way round from
  the (easting, northing) most New Zealand data is written in, unless the
  traditional order is requested. This produces a farm in the wrong place
  rather than an error.
- **LINZ elevation is LERC compressed, and libtiff needs that codec.** Without
  it GDAL will not open a 1 m DEM tile at all:

  ```
  Warning 1: ...: LERC compression support is not configured
  ERROR 1: ...: Cannot open TIFF file due to missing codec LERC.
  ```

  The codec belongs to **tiff**, not to gdal. `gdal[lerc]` enables GDAL's own
  LERC and MRF drivers and does not reach TIFF compression, which is a wrong
  turn worth not repeating. The `gis` feature in `vcpkg.json` asks for
  `tiff[lerc]`; on Linux and macOS the distribution's libtiff already carries
  it. Adding the feature to an existing vcpkg install rebuilds GDAL and its
  dependents, which took 46 minutes here:

  ```bash
  vcpkg install "tiff[core,lerc]:x64-windows" --recurse
  ```

## Both at once: the terrain view

A scenario whose `[terrain]` is a LiDAR snapshot needs the GUI to draw it and
GDAL to read it, and no preset had both until `desktop`:

```bash
cmake --preset desktop -DCMAKE_PREFIX_PATH="/path/to/Qt/6.x;/path/to/vtk"
cmake --build --preset desktop
./build/desktop/bin/paddock-gui data/scenarios/lincoln-lurdf --terrain
```

On Windows, add the same two flags the `gis` section explains - the vcpkg
toolchain file and `-DVCPKG_MANIFEST_MODE=OFF` - so the build takes GDAL from
an install that already exists rather than rebuilding it into `build/desktop`.
The preset cannot carry them because they name paths that belong to a machine
rather than to the project.

A build without the geospatial stack still runs `lincoln-lurdf` up to the point
where it needs the ground, and then refuses it by name. That is deliberate: a
farm quietly running flat when its manifest says otherwise is the failure worth
preventing.

The scenario needs the elevation snapshot, which is not committed:

```bash
python scripts/nz-elevation-snapshot.py --lon 172.470 --lat -43.641 \
  --collection canterbury/selwyn_2023 --out data/snapshots/lincoln-dem-1m.tiff
```

That needs no API key - LINZ publishes elevation as open data. The bundle
records the tile's SHA-256 and the application checks it before reading, so a
different capture is refused rather than quietly substituted.

Two flags exist for looking at this without a person at the screen, and both
are also how it gets checked:

```bash
paddock-gui <bundle> --terrain --heights 5 --screenshot ground.png
```

`--heights` stretches the terrain, and the factor stays on screen because
exaggeration makes every slope look steeper than it is. It is worth having on
LURDF: the ground there falls 6.2 m across 900 m, which drawn true to scale
looks flat, because the Canterbury Plains are. The smoke run prints the
elevation range for the same reason - real ground and a formula look identical
when both are level, and two numbers tell them apart.

## The gates

| Gate | When | Command |
|---|---|---|
| T0 | on save | Format-on-save and clangd diagnostics in both editors |
| T1 | pre-commit | `.githooks/pre-commit` — format, line endings, `ctest --preset fast` |
| T2 | every PR | `.github/workflows/ci.yml` |
| T3 | every PR | Validation against measured growth curves, plot kept as an artifact |

Run any gate by hand:

```bash
scripts/check-format.sh
scripts/check-line-endings.sh
scripts/check-dependency-direction.sh
ctest --preset fast
```

If your clang-format major differs from CI's, point the script at the right
binary: `CLANG_FORMAT=clang-format-19 scripts/check-format.sh`.

### Running clang-tidy before you push

The clang-tidy job is the slowest gate and the one most likely to fail on a
change that builds and passes its tests, so it is worth running locally.

On Windows, Visual Studio ships LLVM 19 - the major CI pins - but in two
copies, and **only the 64-bit one works**. The 32-bit build under
`VC\Tools\Llvm\bin\` segfaults on this project. Use the `x64` path:

```bash
"/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/clang-tidy.exe" --quiet core/src/YourFile.cpp -- -std=c++17 -Icore/include
```

Two cautions when invoking it by hand rather than through the compile database.
Findings in a file whose includes could not be resolved are not to be trusted:
clang-tidy cannot see the real types, so untouched variables look
const-assignable and defined functions look undeclared. And `modernize-type-traits`
fires inside Google Test's own macro expansions, which CI's header filter
suppresses and an ad-hoc run does not. Check a finding against the file's
includes before acting on it.

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
