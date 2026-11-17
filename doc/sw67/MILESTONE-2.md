# Milestone 2, increment 1: first local tools

_Report as of 2026-11-17. Status words are defined in
[ROADMAP.md](ROADMAP.md), which also maps every module to its milestone._

This increment adds the spec's four first priorities: Workspace Profiles,
the Performance Assistant, the Creator Project Manager and manual Product
Photography. It also adds the configurable plan catalogue with its
entitlement rules. All four tools work without any AI service, and their
windows are in **Me > 6|7 Studio**.

## 1. What works

| Tool | Status | What it does |
|---|---|---|
| Workspace Profiles (`sw67_workspaces`) | **Implemented, untested in-world** | Built-in Everyday, Creator, Photographer, DJ and Host layouts, plus your own. **Preview** lists every window it would open, move or close and every preference, toolbar or shortcut change before anything happens. **Apply** then **Undo apply** puts back exactly what changed. **Save the current layout** records open windows and positions, `toolbars.xml` and `key_bindings.xml`. **Export** and **Import** use files you pick; imports are validated and nothing changes until you apply. Accessibility settings, RLV's on/off switch and anything credential-like are never stored in or applied from a profile. |
| Performance Assistant (`sw67_performance`) | **Implemented, untested in-world** | Reads the viewer's own measurements every 3 seconds: frame rate, frame time, ping, packet loss, pending textures, avatars nearby and drawn in full, worn HUDs. Each explanation is labelled **[Measured]** or **[Estimate]**, with the related settings named. The **Everyday, Shopping, Building, Photography and Event** presets preview each setting change, from and to, with a reason. **Apply** saves a restore point first; **Restore previous** puts the earlier values back, even after a restart. Presets can change only eleven of your own graphics settings, never anyone else's objects or settings, and no text promises a frame rate. |
| Creator Project Manager (`sw67_creator_projects`) | **Implemented, untested in-world** | A project is a folder you choose with a `project.sw67.json` manifest beside your files. **Scan folder** adds mesh sources, texture sources, scripts, references and exports, sorted by kind from their extensions. Also: revision history, testing notes, a release checklist (nine default items) and a readiness report of missing files, unticked items, and scripts without testing notes. **Remove from project** never deletes a file. Projects are plain files, so they stay readable without the viewer or a subscription. Inventory links are references with the permissions seen at link time; the project never claims to hold a copy. Linking an inventory item from the window is **Planned**: the manifest format and its tests already support it. |
| Product Photography, manual (`sw67_product_photo`) | **Implemented, untested in-world** | Select an object and choose from seven reusable angles and six export sizes. Set a file-name pattern (`{project}_{angle}_{index:3}`, `{date}`) and an output folder. The window lists every file before you start. **Start batch** moves your camera around the object, after the RLV gate allows a camera change, checking again before each shot. It captures each original as PNG in `originals/` (never overwrites one), writes each size to `exports/<size>/` with a centred crop and resize, and writes a manifest of every file and how it was made. **Stop** keeps what was written, and the camera always returns where it was. **Planned** in the viewer: watermarks, and switching backdrops and lighting looks between passes. The core already plans and tests both. |
| Plan catalogue and entitlements | **Implemented** (logic); not enforced | `app_settings/sw67_plans.json` holds the prices, plans, add-on, AI allowances and credit packs, all editable. It is a sandbox with no billing integration. See [ROADMAP.md](ROADMAP.md#plans-and-entitlements). |

Core logic for all of the above is in `indra/sw67core`:

- `workspace/profile.h`
- `perf/assistant.h`
- `creator/project.h`
- `photo/product.h`
- `plan/entitlements.h`
- `common/settings_policy.h`
- `io/json.h`

It also covers what the viewer windows do not yet show:

- AI-edit labelling (`_ai` in the name, `aiAltered` in the manifest, traced back to the original).
- Watermark placement.
- Backdrop and lighting passes.
- Inventory-link permissions.

## 2. How it was tested

`sw67core` now has **64 test cases and 962 assertions, all passing**. They
built with GCC 13, and every source compiled clean under Clang with
`-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`. The new tests
check the acceptance criteria directly:

- **Accessibility preserved across profiles.**
  - Accessibility, consent and secret settings are dropped on export.
  - The same settings are dropped from hand-edited imports.
  - They are never part of an apply plan.
- **Apply previews are exact and undoable.**
  - A plan lists only real changes, and applying twice changes nothing.
  - The restore point brings back every window and preference.
- **Imports are validated.**
  - Newer formats and other files are refused.
  - Unknown fields are noted.
  - DTDs and entities in toolbar XML are refused, as are window names with `..` and impossible window sizes.
- **Measured apart from estimated.** Every measured finding comes before
  any estimate. No finding is made from a measurement the viewer could not
  read.
- **No guarantees.** No finding, preset reason or disclaimer contains
  "guarantee", "will improve", "will raise" or "will fix".
- **Own settings only.** Presets and restore points name only the eleven
  managed `Render*` settings. A tampered restore point cannot add
  `RestrainedLove`.
- **Projects stay in their folder.**
  - Absolute paths, `..`, drive letters and backslashes are refused.
  - Malformed inventory IDs and revisions are dropped with a reason.
  - A newer manifest is refused with a note that the files are untouched.
- **No false backup claims.** A linked item is described as "keeps no copy".
- **Originals preserved.**
  - Every shot has one PNG original that no step touched.
  - Exports name their original and the steps that made them.
  - Existing files are never reused as names.
  - AI output is always marked and kept apart.
- **Safe file names.** Windows device names, path separators and reserved
  characters are handled. Long UTF-8 names are cut between characters.
- **Plans.**
  - The shipped catalogue matches the spec's prices and assignment.
  - Local data and accessibility work with no subscription.
  - Unlimited AI and include loops are refused.
  - Captioning is on no plan.
- **Hostile JSON.** Deep nesting, oversized files, bad escapes and lone
  surrogates are refused without a crash.

**Not yet tested:** the four viewer windows have not run in a built viewer.
The viewer code was written against the real APIs (`LLFloaterReg`,
`LLToolBarView`, `LLViewerInput`, `LLTrace`, `rawSnapshot`,
`LLAgentCamera`), but the first CI viewer build is its compile check. Once a
build exists, run the in-world checks in section 5.

## 3. Limits and blockers

- **Viewer build pending.** The first CI build was still configuring
  (building vcpkg dependencies) when this increment was written. Expect a
  round of compile fixes in the new `sw67*` viewer files.
- **Entitlements are not enforced** in the viewer, because there is no
  billing service yet. Every tool is open in development builds.
- **Measurements** come from the viewer's own statistics. Texture memory is
  not read yet, so that finding does not appear in the viewer.
- **Product photos** use the current backdrop and lighting. The camera
  waits 1.5 s at each angle for detail to load, which may be too short on a
  slow connection.
- **Workspace toolbars** are per account, so saving or applying them needs
  you to be logged in.

## 4. Build and run

Exactly as in [MILESTONE-1.md](MILESTONE-1.md#4-build-and-run); nothing new
is needed. For the core alone:

```
cmake -S indra/sw67core -B build-sw67core -DSW67_BUILD_TESTS=ON -DCMAKE_PREFIX_PATH=<Catch2 v3 install>
cmake --build build-sw67core --config Release
ctest --test-dir build-sw67core -C Release --output-on-failure
```

In the viewer: **Me > 6|7 Studio > Workspace Profiles / Performance
Assistant / Creator Projects / Product Photography**.

Files the tools write, under the viewer's user settings folder:

- `sw67/workspaces/*.sw67workspace.json`: saved profiles.
- `sw67/performance/restore.json`: the performance restore point.
- `sw67/photos/`: the default photo output folder.
- Creator projects are written wherever you choose.

## 5. In-world checks (once a build exists)

1. **Workspaces.** Apply Photographer. The snapshot, camera, Lightbox and
   environment windows open and nothing else changes. Then Undo. Save a
   layout, export it, delete it, import it, and apply it. Run all of this
   with the Deuteranopia preset and the keyboard only.
2. **Performance.** In a busy club, findings show [Measured] for frame rate
   and [Estimate] for crowd and shadows. Apply Event, check Preferences >
   Graphics reflects it, restart, then Restore previous.
3. **Creator projects.** Choose an empty folder, add files, scan, tick items,
   add a revision and save. Reopen the folder and everything is there. Open
   `project.sw67.json` in a text editor; it is plain JSON.
4. **Product photos.** Rez a box, select it, choose three angles and two
   sizes, then Start. Check `originals/`, `exports/` and the manifest. Run
   it again and the new files are named `-2`. With an RLV device holding
   `@setcam=n` (or similar), Start is refused with the restriction named.
