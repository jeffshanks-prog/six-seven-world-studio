# Milestone 1: working foundation

_Report as of 2026-11-17. Items are marked **Done**, **Done, untested
in-world**, **Pending CI**, or **Not started**._

## 1. What works

| Item | Status | Detail |
|---|---|---|
| Foundation chosen and documented | **Done** | Alchemy fork; reasons, licences and TPV policy in [FOUNDATION.md](FOUNDATION.md). |
| Reproducible viewer build | **Pending CI** | GitHub Actions `Build` workflow (Windows x64, Linux x64, open-source preset). Not yet run; see limits below. |
| Independent branding | **Done, untested in a built viewer** | Name "6\|7 World Studio" (UI), channel `67 World Studio <Test/Beta/Release>`, our own icons and installer art, login-screen wordmark replacing the Second Life Eye-in-Hand, separate settings/cache folder (`SixSevenWorldStudio`), log `SixSevenWorldStudio.log`, About shows the Linden Lab disclaimer and credits Alchemy. |
| Connection to a test grid | **Not started** | Needs a built viewer. Target: Aditi (beta grid). |
| Core-feature smoke checks | **Not started** | Needs a built viewer; checklist below. |
| Accessibility framework | **Done (core tested); viewer UI untested** | `indra/sw67core` (colour maths, presets, validation, settings round-trip) with 23 passing tests; viewer floater **Me > Accessibility (6\|7)**. |
| RLV integration | **Done, untested in-world** | RLVa 2.5.0 inherited; now off by default; automation gate with tests; see [RLV.md](RLV.md). |

### Accessibility framework

- Presets: Standard, **Protanopia**, **Deuteranopia**, **Tritanopia**, High
  contrast, Custom (colour overrides by key).
- Every status (OK, Warning, Error, Info, Idle, Restricted, Live) has a
  colour **and** a distinct shape glyph, icon name and text label, so none
  relies on colour.
- Every built-in preset is checked by test: text contrast WCAG AA 4.5:1
  (high contrast: AAA 7:1), status colours 3:1 against the background (high
  contrast: 4.5:1), and status colours at least 0.08 apart in OKLab both for
  typical vision and as simulated (Machado 2009) for the preset's colour
  vision type.
- Text scale 0.75x to 3x (clamped), keyboard focus highlight, reduce motion,
  stored in the `SW67Accessibility` setting.
- The 3D **scene colour filter** is a separate setting from the interface
  palette. It drives Alchemy's existing CVD compensation pass
  (`RenderCVDMode`), and is **left out of snapshots** unless the user ticks
  "Also apply the filter to snapshots" (`SW67SceneFilterInSnapshots`,
  enforced in `LLPipeline` at the final blit).
- Not yet done: the text scale is stored but not yet applied to the viewer's
  font registry; existing (non-6|7) floaters keep their skin colours. New 6|7
  modules read the framework from the start.

## 2. How it was tested

- `sw67core` tests, Catch2 v3, built with GCC 13 (`-Wall -Wextra
  -Wpedantic`) and checked with Clang 18 (`-Wshadow -Wconversion -Werror`):
  **23 test cases, 250 assertions, all passing.** Contrast values are
  checked against WCAG anchor values (21:1, #767676 = 4.54:1).
- The RLV behaviour names used by the gate were checked against RLVa's
  dictionary in the source.
- **The full viewer has not been compiled yet.** The cloud sandbox this was
  written in cannot download the vcpkg dependencies (GitHub archive and
  release downloads are blocked there). The viewer-side files
  (`sw67accessibility`, `sw67floateraccessibility`, `sw67rlvstate`, the
  branding and pipeline edits) were written against the actual APIs but
  have not been through a compiler. The first CI run is that check.

## 3. Platform limits and remaining blockers

- **Build:** needs the first CI run (or a local Windows build). Expect a few
  rounds of compile fixes in the new viewer files.
- **Actions minutes:** the repository is private. A viewer build takes
  roughly 2 to 4 hours per platform on GitHub's runners, and Windows minutes
  bill at twice the Linux rate. Builds therefore run only on demand.
- **Test grid:** logging in to Aditi needs a Second Life account; nothing in
  6|7 services touches the password.
- **Attachments:** Second Life allows 38 worn attachments, not 45.
- **Before any public or paid release:** the source must be available to
  recipients (LGPL), a privacy policy must exist (TPV policy 4.b), and the
  disclaimer must be on the download page. Code signing needs certificates.
- macOS and arm64 builds are not in CI yet.

## 4. Build and run

### Windows (Visual Studio 2026)

Prerequisites: Visual Studio 2026 with **Desktop development with C++**,
CMake 4.0 or newer, Git for Windows. About 40 GB free disk.

```
git clone --recurse-submodules https://github.com/jeffshanks-prog/six-seven-world-studio.git
cd six-seven-world-studio
cmake -S indra --preset vs2026-os
cmake --build build-Windows-vs2026-os --config Release
```

The first configure builds every dependency with vcpkg (30 to 60+
minutes). The viewer is
`build-Windows-vs2026-os\newview\Release\67WorldStudioTest.exe`.

### Linux (Ubuntu 22.04+)

Install the packages listed under "Ubuntu 22.04+" in [../BUILD.md](../BUILD.md), CMake 4.0+, then:

```
git clone --recurse-submodules https://github.com/jeffshanks-prog/six-seven-world-studio.git
cd six-seven-world-studio
cmake -S indra --preset ninja-os
cmake --build --preset ninja-os-release
./build-Linux-ninja-os/newview/Release/67WorldStudioTest
```

### CI

GitHub > Actions > **Build** > Run workflow (channel "Test"). Artifacts are
the Windows installer and the Linux package. **sw67core** runs on every push
that touches `indra/sw67core`.

### sw67core alone (seconds, no viewer dependencies)

```
cmake -S indra/sw67core -B build-sw67core -DSW67_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=<Catch2 v3 install>
cmake --build build-sw67core && ctest --test-dir build-sw67core
```

### Logging in to the beta grid (Aditi)

Start the viewer with `--grid aditi`, or enable the grid selector in
Preferences and choose Aditi on the login screen.

## Smoke-check list (to run once a build exists)

Login (Agni and Aditi), rendering a busy region, inventory open/search,
wear and remove an outfit, build a prim and edit it, local chat and IM,
voice (WebRTC) in a voice-enabled parcel, teleport by landmark and map,
object permissions (no-copy, no-mod), RLV off then on with an RLV device:
`@tploc=n` blocks a teleport and the 6|7 floater shows the restriction.
