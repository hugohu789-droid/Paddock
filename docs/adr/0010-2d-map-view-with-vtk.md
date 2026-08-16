# ADR 0010 — VTK renders the 2D map view, on Ubuntu 26.04's Qt6 packaging

- **Status:** accepted
- **Date:** 2026-08-17
- **Milestone:** M2

## Context

`CLAUDE.md` names VTK for both views: "Visualisation: VTK (2D map view + 3D
terrain view; livestock as `vtkGlyph3D` instances)", and the application is Qt6.
M2 asks for the 2D map view with a colour scale and timeline playback.

The question was whether M2's 2D view should use VTK or draw the raster with
Qt alone, and it turns on one thing: whether a Qt6 application can link the
VTK Qt widgets on the machines that have to build it.

**Local machine.** Qt 6.10.1 (msvc2022_64) and a VTK 9.5 build that includes
`vtkGUISupportQt`. Both paths work here.

**CI.** This is where it was nearly decided the other way. On the platforms this
project was building on, VTK's Qt integration is packaged against Qt5, and a
Qt5 `QVTKOpenGLNativeWidget` cannot be embedded in a Qt6 application:

| Platform | VTK package | Qt it links |
|---|---|---|
| [Ubuntu 24.04](https://packages.ubuntu.com/noble/libvtk9-qt-dev) | 9.1.0 | **Qt5** (`qtbase5-dev`, `libqt5opengl5-dev`) |
| [Debian trixie](https://packages.debian.org/trixie/s390x/libvtk9-qt-dev) | 9.3.0 | **Qt5** (`qtbase5-dev`, `libqt5opengl5-dev`) |
| [Ubuntu 26.04](https://packages.ubuntu.com/resolute/libvtk9-qt-dev) | **9.5.2** | **Qt6** (`qt6-base-dev`) |

Ubuntu 26.04 is the release where Debian's VTK packaging moved to Qt6, and it
ships VTK 9.5.2 with `qt6-base-dev` 6.10.2 — within a patch release of what this
project already builds against locally. GitHub Actions offers an `ubuntu-26.04`
runner image, and an official `ubuntu:26.04` container exists.

So the cost that would have justified avoiding VTK — building it from source on
every pull request, tens of minutes — does not exist. It is an `apt install`.

## Decision

**VTK renders the 2D map view**, as `CLAUDE.md` specifies, and the GUI CI job
runs on Ubuntu 26.04 where Qt6 and VTK 9.5 install from the distribution.

- `viz/` owns the VTK pipeline: a `vtkImageData` built from a core `Raster<T>`,
  a lookup table for the colour scale, and a `vtkScalarBarActor` for the legend.
- **The value-to-colour mapping is a pure function with no VTK in it**, unit
  tested headlessly. The part that can be quantitatively wrong — which value
  gets which colour, what happens at the ends of the range, what an empty
  raster does — is tested without a display or a graphics driver.
- `app/` embeds `QVTKOpenGLNativeWidget` and owns the timeline.
- The GUI job builds the application and runs a smoke test under `xvfb-run`,
  because VTK needs a GL context to render even a 2D image.
- The three-platform *packaging* matrix still lands in M3 with the 3D view, as
  [ADR 0006](0006-ci-scope.md) said. What changes here is that a Linux GUI build
  gate exists from M2 rather than from M3.

## Consequences

- One rendering stack for both views. M3's 3D terrain view, DEM relief and
  `vtkGlyph3D` livestock reuse what M2 builds rather than replacing it.
- The GUI job is pinned to `ubuntu-26.04`, which GitHub currently marks
  **preview**. If that image becomes unavailable or unstable, the job moves to
  `container: ubuntu:26.04` on a stable runner — same packages, same versions,
  no dependency on the runner image's lifecycle. That is the fallback, and it is
  cheap to take.
- Developers on Ubuntu 24.04 cannot build the GUI from distribution packages;
  they need Ubuntu 26.04, a VTK built with Qt6, or vcpkg. `docs/setup.md` says
  so, and the core, config and simulation work all still build and test without
  the GUI (`PADDOCK_BUILD_VIZ=OFF` is the default).
- Rendering in CI needs `xvfb`. That is one apt package and one command prefix,
  but it means a GUI test failure can be a graphics-stack failure rather than a
  code failure, and the job's output has to be read with that in mind.

## What changed this decision

The first draft of this ADR chose Qt-only rendering, on the evidence that every
distribution package of VTK's Qt integration was Qt5. That evidence was correct
and the conclusion did not survive one more question: *which Ubuntu?* Ubuntu
26.04 had moved to Qt6 packaging. The lesson is recorded here rather than
quietly rewritten, because the first draft's reasoning was sound and only its
premise was stale.
