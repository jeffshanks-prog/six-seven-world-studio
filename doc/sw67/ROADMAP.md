# 6|7 World Studio roadmap

_As of 2026-11-17. This maps every module of the product spec, the original
thirteen and the fifteen added on 2026-11-17, to a milestone, to the code it
builds on, and to its status._

**Status words.** **Implemented**: code exists and its logic is covered by
tests that ran. **Implemented, untested in-world**: as above, but its viewer
window has not yet been run in a built viewer. **Experimental**: works in a
limited form, labelled as such in the UI. **Planned**: not written yet.
Nothing is called complete without code and tests that ran.

Where a module lives:

- `indra/sw67core`: logic with no viewer dependencies, unit tested on its own
  (`ctest`, 111 test cases as of 2026-11-17).
- `indra/newview/sw67*`: thin viewer windows and adapters over sw67core and
  the Alchemy systems below.

## Milestones

| Milestone | Scope |
|---|---|
| **M1 Foundation** | Reproducible build, branding, test-grid login, smoke checks, accessibility framework, RLV. |
| **M2 Local tools** | Everything that runs on the user's machine with no AI and no paid service. Built in three increments, in the spec's priority order (below). |
| **M3 AI creation** | AI Director, AI Scene Director, AI Mesh, Script and Texture Studios, AI product photography, listing drafts, and the metering they share. |
| **M4 Events and media** | DJ and Hosting Workspaces, Event Rehearsal, Stage and Lighting, Performance Sequencer, Live Captioning. |
| **M5 Subscription release** | Billing service, entitlement checks against it, privacy policy, public source release, signed installers. |

M2 increments, as the spec prioritises them:

1. **First** ([MILESTONE-2.md](MILESTONE-2.md)): Workspace Profiles, Performance Assistant,
   Creator Project Manager, Product Photography (manual).
2. **Next** ([MILESTONE-2.2.md](MILESTONE-2.2.md)): Outfit Planner, Pose and Photo Studio, Event Rehearsal (its
   simulation core; live execution is M4), Recording Privacy Mode, Settings
   Backup and Restore.
3. **Then**: the manual halves of Stage and Lighting and the Performance
   Sequencer, ahead of their M4 live control; AI Director and Scene
   Director come in M3, Captioning in M4.

## Module map

| # | Module | Milestone | Builds on (existing code) | Plan | Status |
|---|---|---|---|---|---|
| A | Accessibility framework | M1 | Alchemy CVD pass (`RenderCVDMode`), `UIScaleFactor` | Always included | **Implemented** (core); viewer window untested in-world |
| B | RLV / RLVa | M1 | RLVa 2.5.0 (`indra/newview/rlv*`) | Always included | **Implemented, untested in-world** (gate tested) |
| — | King Jahmir AI guide | M2 / M3 | Gateway `services/king-jahmir` (Claude API), help pages and registry `doc/sw67/help`, `sw67core/assistant`, `sw67floaterkingjahmir` | Basic (AI metering with M3) | **Implemented**: gateway and core, tested; viewer panel and bubble **untested in-world**; account tools wait for an Avatar Life records service ([KING-JAHMIR.md](KING-JAHMIR.md)) |
| 14 | Workspace Profiles | M2.1 | `LLFloaterReg`, floater rects, `toolbars.xml` (`LLToolBarView`), `key_bindings.xml` (`LLViewerInput`) | Basic | **Implemented, untested in-world** |
| 8 | Performance Assistant | M2.1 | `LLTrace` frame recording (`LLStatViewer::FPS`, `FRAMETIME`, `SIM_PING`, `PACKETS_LOST_PERCENT`), `LLTextureFetch`, avatar impostor state; graphics settings `LLPresetsManager` manages | Basic | **Implemented, untested in-world** |
| 5 | Creator Project Manager | M2.1 | Script Studio (`alfloaterscriptstudio`, its external-file masters) for scripts later | Basic (files never gated) | **Implemented, untested in-world**; inventory linking from the window **Planned** (manifest and tests support it) |
| 6 | Product Photography (manual) | M2.1 | `LLViewerWindow::rawSnapshot`, `LLAgentCamera`, `LLSelectMgr` bounding box, Lightbox looks | Basic | **Implemented, untested in-world**: angles, sizes, naming, originals kept, exports, manifest. Watermarks in the viewer, backdrop and lighting switching **Planned** (planned and tested in core) |
| 6b | AI product photography | M3 | Same, plus the AI service | AI Creator | **Planned**; output labelling and reversible edit chain implemented in core |
| 4 | Outfit Planner | M2.2 | Outfits and COF (`llappearancemgr`, `llagentwearables`), RLV gate (Wear/Remove), attachment limit (`LLAgentBenefits`) | Basic | **Implemented, untested in-world** |
| 3 | Pose and Photo Studio | M2.2 | Snapshot floater, `LLAgentCamera`, product photo's camera maths and file naming, own-avatar animations (`LLAgent::sendAnimationRequest`) | Basic | **Implemented, untested in-world** (manual; posing others with their consent **Planned**) |
| 13 | Recording Privacy Mode | M2.2 | Name tag, toast, navigation bar and balance settings; perf assistant's change plans | Basic | **Implemented, untested in-world** |
| 15 | Settings Backup and Restore | M2.2 | `settings_policy`, `LLControlGroup`, every 6\|7 file folder, toolbars and shortcuts | Basic | **Implemented, untested in-world** |
| 9 | Event Rehearsal Mode | M2.2 core, M4 live | RLV gate's action traits (sends, spends, publishes) | DJ & Hosting | Rehearsal **Implemented, untested in-world**; running an event live **Planned** (M4) |
| 10 | Stage and Lighting Controller | M2.3 / M4 | Scripted-object chat channels, RLV gate, accessibility `reduceMotion` | DJ & Hosting | **Planned** |
| 11 | Dance and Performance Sequencer | M2.3 / M4 | Animation permissions, RLV gate | DJ & Hosting | **Planned** |
| 1 | AI Director | M3 | RLV gate (`evaluateBatch`, `describeStop`), explicit-authorisation traits | Basic allowance; expanded with AI Creator | **Planned** |
| 2 | AI Scene Director | M3 | Product photo camera placement, Lightbox, environment presets | AI Creator | **Planned** |
| 7 | Marketplace Listing Assistant | M3 | Creator project manifest (facts the creator supplied) | AI Creator | **Planned** |
| 12 | Live Captioning and Translation | M4 | Voice (`llwebrtc`) | Not on a plan until costs are measured | **Planned** |
| — | Automation Center | M3 | RLV gate | Basic | **Planned** |
| — | Photo/Video Studio | M2.2 | Snapshot floater, Lightbox, Alchemy post-processing | Basic | **Planned** (Product Photography is its first part) |
| — | Inventory Organizer | M2.2 | `llinventorymodel`, RLV folder locks | Basic | **Planned** |
| — | AI Mesh, Script, Texture Studios | M3 | Script Studio, mesh uploader | AI Creator | **Planned** |
| — | AO Builder | M2.2 | Alchemy AO, animation overrides | Basic | **Planned** |
| — | 6\|7 Phone | M3 | IM, `LLNotifications` | Basic | **Planned** |
| — | Communication (up to 50 recipients) | M3 | IM sessions; RLV gate (SendIM) | Basic | **Planned**; recipient caps to be verified per message type |
| — | Attachment Manager | M2.2 | Attachments; SL limit verified at 38 worn (45 target is not possible) | Basic | **Planned** |
| — | DJ Workspace, Hosting Workspace | M4 | Media and stream settings; workspace profiles (built-in DJ and Host layouts **Implemented**) | DJ & Hosting | **Planned** |
| — | Billing and credits | M5 | `sw67core/plan` (catalogue and entitlements **Implemented**, tested) | — | Entitlement logic **Implemented**; billing service **Planned** |

## Plans and entitlements

The catalogue is `indra/newview/app_settings/sw67_plans.json`, read by
`sw67::plan::readCatalogue` and tested as shipped. Changing a price, a
plan's features or an allowance is an edit to that file.

| Plan | Price | Includes |
|---|---|---|
| Basic | $19.99/month | Workspace profiles, performance assistant, creator projects, manual product photography and pose tools, outfit planner, recording privacy, settings backup, small AI Director allowance |
| DJ & Hosting | $29.99/month | Basic, plus event rehearsal, stage and lighting, performance sequencing, DJ and hosting workspaces |
| AI Creator add-on | +$14.99/month | Expanded AI Director, AI scene planning, AI product photography and listing drafts, AI mesh, script and texture allowances |
| Credit packs | from $5 | Extra metered AI credits |

Rules the code enforces, each with a test:

- **Local data is never gated.** Projects, photos, profiles and backups are
  the user's files; opening and exporting them needs no subscription, so they
  stay usable after cancelling. Accessibility is never gated either.
- **No unlimited AI.** A catalogue offering it is refused. Allowances are
  numbers and add up across plan and add-on; DJ & Hosting does not count
  Basic's allowance twice, and event modules bring no AI or transcription.
- **Captioning has no paid plan** until its costs are measured, so no
  recurring charge exists for it.
- **Sandbox.** The catalogue says `"sandbox": true`; there is no billing
  integration yet, so no charge is possible. `trialDays` is `null` because no
  trial length has been decided.
- AI credit numbers (20 a month on Basic, 300 with AI Creator, 100 for $5)
  are **placeholders** until per-action costs are measured.

Not yet enforced: the viewer does not check entitlements, because there is
no billing service to ask. Every feature is open in development builds.
That check arrives in M5.

## Shared components added in this increment

- `sw67/io/json.h`: one bounded JSON reader and writer for every 6|7 file
  (profiles, projects, photo manifests, restore points, the plan catalogue).
  It limits size, depth and string length, and never throws on bad input.
- `sw67/common/settings_policy.h`: the single rule for which settings may go
  into or come out of a file. Secrets (any name containing password, token,
  secret, credential, session ID, or an API, stream, auth or private key), accessibility settings and consent opt-ins
  such as RLV are excluded. Workspace profiles use it now; Settings Backup
  will.
- `sw67viewerfiles.h`: our files under `<user settings>/sw67/`, read with a
  size cap and written through a temporary file so a crash never leaves half
  a file.
