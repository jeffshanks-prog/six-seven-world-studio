# Milestone 2, increment 2: the next five local tools

_Report as of 2026-11-17. Status words are defined in
[ROADMAP.md](ROADMAP.md). This increment is built on main after King Jahmir
(PR 2) was merged._

This increment adds the spec's second priority group:

- Outfit Planner
- Pose and Photo Studio
- Event Rehearsal Mode
- Recording Privacy Mode
- Settings Backup and Restore

None of them needs an AI service or a paid service. Their windows are in
**Me > 6|7 Studio**, beside the four tools from increment 1.

## 1. What works

### Settings Backup and Restore

`sw67_backup` — **Implemented, untested in-world**.

- **Save backup** writes one plain JSON file wherever you choose. It holds:
  - every setting you changed from its default, for this computer and for
    the logged-in account;
  - your 6|7 files: workspace profiles, the performance restore point,
    outfits, photo shoots, event plans and privacy mode's state;
  - your toolbar layout and keyboard shortcuts.
- **Never in a backup:**
  - passwords, tokens, API keys and stream keys;
  - any text that looks like a credential, such as `user:password@` in an
    address or `token=` in a query;
  - consent opt-ins such as RLV and captioning;
  - this computer's own folders, version and window size.
- Accessibility settings go in unless you untick them.
- **Open backup** shows exactly what restoring would change, setting by
  setting and file by file, before anything happens. Settings the viewer
  does not have, or that hold another type of value, are listed and
  skipped.
- **Restore** saves an undo point first. **Undo restore** puts the settings
  back as they were.
- A restore never deletes anything. A file you have that differs from the
  backup is kept unless you choose to replace it, and even then your copy is
  kept beside it as `.before-restore`.

### Recording Privacy Mode

`sw67_recording_privacy` — **Implemented, untested in-world**.

One switch hides, on your screen only:

- your own name tag;
- other people's name tags and group titles;
- nearby chat, IM, group, conference and object pop-ups (messages still
  arrive in your conversation windows);
- chat bubbles;
- your location in the navigation bar;
- your L$ balance, in the status bar and in snapshots;
- hover tips.

Each can be left showing. The window previews every change before you turn
it on. Turning it off puts back exactly what it changed, except any setting
you changed yourself while it was on, which it lists and leaves as you set
it.

Its state is saved before anything changes, so it can be turned off after a
crash or restart. Whether it is on is shown in words with the Live shape,
not by colour.

It sends nothing, changes nobody else's view, and does not start, stop or
detect a recording.

### Outfit Planner

`sw67_outfit_planner` — **Implemented, untested in-world**.

- Outfits are plain files you keep. Each has a name, occasion, tags, notes
  and the items it uses, by inventory ID.
- Start one from what you wear now, or add items selected in your
  inventory.
- **The preview** shows every item worn, taken off, kept or missing.
- **Wearing an outfit** can replace what you wear (take off what the outfit
  lacks) or add to it. It always keeps one shape, skin, hair and eyes, and
  the option "Keep my HUDs on" is on by default.
- **Limits** are checked before you wear anything: your account's
  attachment limit (38 on Second Life today) and the 60 clothing layers.
- **RLV** is asked about every item first. If any is blocked, nothing
  changes.
- **Nothing is deleted, bought or moved.** An item you no longer have is
  listed as missing; it is never bought or replaced for you. Removing an
  outfit removes only its plan file, kept as `.deleted`.

### Pose and Photo Studio, manual

`sw67_pose_studio` — **Implemented, untested in-world**.

- **Twelve framings** of your own avatar, worked out from where you stand,
  which way you face and your height:
  - headshot, head and shoulders, half, three-quarter and full body;
  - low and high angles;
  - three-quarter turns and both profiles;
  - from behind.
- **Save this camera** stores a shot relative to you, so it repeats
  wherever you stand.
- **Poses** are animations from your inventory. Step through them with
  Previous, Pose and Next; Stop pose ends one.
- Poses play on your own avatar only. Posing someone else with their consent
  is **Planned**.
- The camera moves only after the RLV gate allows it.
- A shoot (its shots and poses) saves as a plain file. **Snapshot** opens
  the viewer's snapshot window.

### Event Rehearsal Mode

`sw67_event_rehearsal` — rehearsal **Implemented, untested in-world**;
running an event live is **Planned** for Milestone 4.

- **The run sheet** is a list of timed cues:
  - nearby chat;
  - IMs and invitations;
  - gifts and L$ prizes;
  - starting and stopping a stream;
  - changing your own sky and water;
  - camera moves;
  - notes for the host.
- **Check and rehearse** lists everything the event would do, in time
  order, and checks it:
  - every message, invite, gift and prize has someone to go to;
  - chat and IM text is within Second Life's limits;
  - prizes are at least L$1 and stay within the budget;
  - cues fall within the planned length.

  It also asks RLV, so a cue a current restriction would block shows before
  the event.
- **Run rehearsal clock** plays the cues in real time, or 10 or 60 times
  faster.
- **Rehearsal cannot send, broadcast, invite or pay.** Its logic only
  returns descriptions ("Rehearsal: would pay L$500 to Sam"), and the window
  includes none of the viewer's chat, IM, payment or media code. A banner
  says so, and so does every report.

### Core logic

All of it is in `indra/sw67core`:

- `backup/backup.h`
- `privacy/recording.h`
- `outfit/planner.h`
- `pose/studio.h`
- `event/rehearsal.h`

These reuse the shared pieces from increment 1:

- the settings policy (backups follow the same rule as workspace profiles);
- the JSON reader;
- the performance assistant's change plans (privacy mode);
- product photography's file naming (pose studio);
- the RLV gate (outfits, poses, rehearsal).

## 2. How it was tested

`sw67core` has **111 test cases, all passing**. That is 38 new cases since
increment 1, plus King Jahmir's tests. The new sources compile clean under
Clang with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror`.

The tests check the spec's rules directly:

- **No credentials in a backup.**
  - Password, token, API-key and stream-key settings are refused.
  - So is a URL with `user:password@`.
  - The written file is searched for the secret values and none is found.
- **A hand-edited backup is checked again when it is read.**
  - It cannot switch RLV on or carry a password.
  - Files outside our folders are dropped, as are `..` paths and files over
    1 MB.
  - Unknown fields are noted.
- **Restore.**
  - The preview lists exactly what changes.
  - Restoring twice changes nothing.
  - The undo point brings back the earlier values.
  - A file that is not in the backup is never touched.
- **Privacy mode.**
  - It changes only its fifteen display settings.
  - Turning it off returns every value exactly.
  - It keeps a setting the user changed meanwhile.
  - Its saved state cannot be tampered into another setting.
- **Outfits.**
  - Replacing and adding each give the right wear, take-off and keep steps.
  - Body parts are swapped, never left missing.
  - HUDs and items marked to keep stay on.
  - 39 attachments are refused at a limit of 38.
  - Missing items are listed and never bought.
  - The plan has no delete step.
  - Bad file entries are dropped with a reason.
- **Poses.**
  - Every framing shows the span it names, from the side it names, at any
    facing.
  - Saved shots repeat after the avatar moves and turns.
  - Pose stepping wraps.
  - Shots more than 100 m away, and malformed IDs, are dropped.
- **Rehearsal.**
  - Every outward cue is described as "would".
  - Invites, broadcasts and prizes are counted, never made.
  - RLV blocks show in the report.
  - The clock releases each cue once, in order, and never goes back.
  - Missing recipients, a zero prize, an over-budget total and over-long
    chat are all caught.

**Not yet tested:** none of the five windows has run in a built viewer. The
first CI build of this increment is their compile check. Then run the
in-world checks in section 5.

## 3. Limits and blockers

- **Viewer build pending.** Expect a round of compile fixes in the five new
  windows.
- **Backups** cover boolean, number and text settings. Colours, vectors and
  rectangles (a few dozen, mostly debug) are not included yet. Some restored
  settings take effect after a restart.
- **Privacy mode** hides through the viewer's own display settings. Pop-ups
  already on screen when you turn it on stay until they fade.
- **Outfit Planner** wears items with the viewer's own outfit code, so the
  order of clothing layers follows the viewer's rules, not the outfit file's.
- **Pose studio** plays animations as the viewer always does, so people
  nearby see your pose. Posing others needs their consent through an
  in-world script, which is not built yet.
- **Rehearsal** is the whole of Event Rehearsal for now. Running an event for
  real is Milestone 4, and will ask your authorization before every message,
  broadcast and payment.

## 4. Build and run

Exactly as in [MILESTONE-1.md](MILESTONE-1.md#4-build-and-run). For the
core alone, see [MILESTONE-2.md](MILESTONE-2.md#4-build-and-run).

In the viewer: **Me > 6|7 Studio > Settings Backup and Restore / Recording
Privacy Mode / Outfit Planner / Pose and Photo Studio / Event Rehearsal**.

New files the tools write, under the viewer's user settings folder:

- `sw67/outfits/*.sw67outfit.json`
- `sw67/photoshoots/*.sw67photoshoot.json`
- `sw67/events/*.sw67event.json`
- `sw67/privacy/state.json`, only while privacy mode is on
- `sw67/backups/before-restore.sw67backup.json`, the undo point
- Backups themselves are saved wherever you choose.

## 5. In-world checks (once a build exists)

1. **Backup.** Save a backup, open it in a text editor and search for your
   password, any stream key and `RestrainedLove`. None is there. Change a
   graphics setting, open the backup, check the preview lists that change,
   Restore, then Undo restore.
2. **Privacy.** Turn it on with a friend nearby. Name tags, IM pop-ups, your
   location and your balance are hidden, and your friend still sees your
   name. Turn it off and everything is back. Turn it on, quit the viewer,
   start again and turn it off.
3. **Outfits.** Save what you wear. Take off a jacket, add a new hair from
   inventory, check the preview, then Wear. Wear an outfit with more
   attachments than your limit and it is refused. With an RLV device holding
   `@remoutfit=n`, Wear changes nothing and names the restriction.
4. **Pose studio.** Try Headshot, Full body and Profile, left. Save a shot,
   walk 20 m and turn, then Go to shot. Add two animations, step through them
   and Stop pose.
5. **Rehearsal.** Make a five-cue plan with an IM, a prize and a stream.
   Check and rehearse, then run the clock at 60x. No IM arrives, no L$ moves
   and no stream starts.
