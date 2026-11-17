# Foundation decision

_Decided 2026-11-17. Status: in effect._

6|7 World Studio is a fork of the **Alchemy Viewer**
(<https://github.com/AlchemyViewer/Alchemy>), taken at upstream commit
`00cdb33ca75334fcd5a2146b3b4669cd45c393bf` (2026-11-14, viewer 26.4.0).
The full upstream history is kept, so upstream fixes merge with
`git fetch upstream && git merge upstream/main`.

## Candidates considered

| Viewer | Licence | RLV | Build system | Notes |
|---|---|---|---|---|
| Linden Lab viewer (`secondlife/viewer`, 26.5.0) | LGPL-2.1 | None | autobuild + CMake | Reference implementation. RLV would have to be ported in. |
| Firestorm (`FirestormViewer/phoenix-firestorm`, 26.4.0) | LGPL-2.1 (some files carry their own header) | RLVa | autobuild + CMake | Largest feature set and code base; strongest brand to scrub; heavier to keep merged. |
| **Alchemy** (`AlchemyViewer/Alchemy`, 26.4.0) | **LGPL-2.1** | **RLVa 2.5.0 (RLV API 3.4.3), LGPL-2.1** | **CMake 4 + vcpkg presets** | Tracks LL's current code (WebRTC voice, PBR), Linux/Windows/macOS, open-source build path with no proprietary parts, a clean preset-driven build that CI can reproduce. |

**Why Alchemy:** it already carries RLVa under the LGPL (Milestone 1 needs
RLV integrated, not ported), it stays close to Linden Lab's current code, and
its CMake/vcpkg build is the most reproducible of the three. Its own
features (Script Studio, Lightbox post-processing, a CVD compensation pass)
line up with our Photo/Video and Script Studio modules.

## Licences, verified in the source

- Viewer source: `LICENSE` and `doc/LGPL-license.txt`, **GNU LGPL 2.1**.
  Linden Lab's artwork licence: `doc/LICENSE-logos.txt`.
- RLVa (`indra/newview/rlv*.cpp/h`): Copyright Kitty Barnett, **LGPL 2.1**
  (file headers).
- Our own code (`indra/sw67core`, `indra/newview/sw67*`): LGPL 2.1, so the
  viewer stays one licence.
- **Alchemy branding** (`AlchemyViewer/alchemy-branding`): _"Copyright (c)
  2025 Alchemy Development Group. All Rights Reserved."_ **Not used.** It is
  removed from `indra/vcpkg.json`; our artwork lives in
  `indra/newview/sw67branding/` (drawn by `scripts/branding/make_icons.py`).
- Bundled third-party components in an open-source (`-os`) build, from the
  vcpkg ports' declared licences: CEF (BSD-3-Clause), WebRTC (BSD-3-Clause),
  VLC binaries (LGPL-2.1, Windows/macOS only), OpenJPEG (BSD-2-Clause),
  fonts (OFL-1.1, Bitstream Vera, MIT, CC-BY-4.0), NVAPI (MIT), SLua and
  lsl-definitions (MIT), and the usual permissive libraries (Boost, curl,
  OpenSSL 3 under Apache-2.0, zlib, libpng, FreeType, HarfBuzz, SDL3, ...).
  vcpkg writes every port's copyright file into the install tree; the
  packages ship them.
- **Not used (proprietary, would need licences):** FMOD Studio, Alchemy's
  pnj2k codec, the Discord Social SDK. These are only reachable with
  `AL_ENABLE_PROPRIETARY=ON`, which our presets and CI never set. Audio is
  FAudio/OpenAL; JPEG 2000 is OpenJPEG; physics uses the open
  `llphysicsextensionsos` stub (no Havok).

### Paid distribution

The LGPL 2.1 allows selling the viewer. Obligations we accept:

1. Every recipient gets the corresponding source of the LGPL code we
   distribute (offer it with each download; the repository has to be public,
   or the source otherwise provided, **before the first binary leaves the
   building**).
2. Keep all licence texts and copyright notices (kept; the About floater's
   licences tab and `doc/` carry them).
3. Our changes to LGPL files stay LGPL.

**Consequence for the business model:** because the viewer source is open,
anyone can rebuild it without the subscription checks. Paid value has to live
in server-side services that cannot be copied from the source: the AI
gateway and credit ledger, cloud rendering/modeling jobs, hosted event and
streaming integrations, and support. Viewer-side entitlement checks are a
convenience, not protection. Manual editing and importing stay in the core
viewer, as the product brief already requires.

## Second Life Third Party Viewer Policy

Read 2026-11-17 at <https://secondlife.com/corporate/third-party-viewers>
(the page shows no revision date; footer © 2022). How the fork complies:

| Policy | Status |
|---|---|
| 1.c Disclose name and the disclaimer _"This software is not provided or supported by Linden Lab, the makers of Second Life."_ | Disclaimer added to About (and package descriptions). Also needed on the download page. |
| 1.g Release name and version on the login screen and About | Window title shows name and version from the login screen on (release builds now included); About shows both. |
| 2.a Do not circumvent permissions or alter creator/owner metadata | No such feature. Inventory Organizer and Mesh Studio (later milestones) must use normal permission checks. |
| 2.b Export only when creator = user for every component | Applies to Mesh Studio export; to be enforced there. |
| 2.c.ii Do not spoof the viewer identifier | Channel is our own: `67 World Studio <Test/Beta/Release>`. |
| 2.e Send SL usernames/passwords only to Linden Lab; store them only on the user's machine | Login is untouched Linden Lab login code. 6|7 services (billing, AI, analytics) never receive SL credentials. |
| 2.k No feature that alters the shared experience beyond what LL's viewer allows | Relevant to the 45-attachment request: the server limit is 38 (see below); we will not fake more. |
| 4.b Published privacy policy; no user data needed to install/uninstall | Privacy policy still to be written (blocker for any public release). |
| 5 Name not confusingly similar to LL marks; no Eye-in-Hand logo | Login-screen Eye-in-Hand images replaced with our wordmark; name contains no LL mark. |
| 6 Viewer Directory (optional): self-certify, public repo, public commits | Optional. Needs the repository public. |

## Platform limits verified so far

| Item | Finding | Source |
|---|---|---|
| Worn attachments | **38** combined HUD and body attachments (47 attachment points exist) | <https://wiki.secondlife.com/wiki/Limit>, read 2026-11-17 |
| Group memberships | 50 Basic, 55 Plus, 80 Premium, 150 Premium Plus | same |
| Voice conference size | Not stated on the Limit page; still to verify (WebRTC voice) | open |
| Ad-hoc text conference size | Not stated on the Limit page; still to verify | open |

The requested 45 simultaneous attachments is **not supported** by the
server limit of 38; the Attachment Manager (Milestone 2) will say so.
