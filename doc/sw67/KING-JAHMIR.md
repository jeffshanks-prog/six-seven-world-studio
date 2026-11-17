# King Jahmir, the 6|7 AI guide

_Report as of 2026-11-17. Status words are defined in [ROADMAP.md](ROADMAP.md)._

King Jahmir is the viewer's conversational guide. He answers questions from
the 6|7 help pages and feature registry, asks follow-up questions, offers to
open app windows, and drafts support requests the resident confirms. He is
an AI assistant and says so.

## 1. How it fits together

```
Viewer (C++)                           Gateway (Python, services/king-jahmir)        Claude API
crown bubble + conversation panel  ->  token check, rate limits, retries        ->  claude-opus-5-5
sw67core/assistant (protocol,           tool loop, server-side tool checks
reply rendering, state)                 help pages + feature registry (doc/sw67/help)
token in protected store                account provider (UNAVAILABLE today)
                                        SQLite: conversations, drafts, preferences
```

- The viewer holds no AI key. Keys and privileged credentials stay on the
  gateway.
- Documentation retrieval (`search_help`, `get_feature_status`) is separate
  from permission-controlled account tools (`read_my_account`,
  `owner_records`).
- Retrieved text reaches the model as reference data marked untrusted, never
  as instructions.

## 2. What is connected and working

| Part | Status | Notes |
|---|---|---|
| Gateway: auth, rate limits, timeouts, idempotent retries, safe errors | **Implemented** (37 tests passing) | HS256 session tokens; per-resident and per-address limits, daily cap, one message at a time; model timeout 60 s with 2 SDK retries; same `Idempotency-Key` replays the stored answer. |
| Gateway: Claude tool loop | **Implemented**, tested against the real Anthropic SDK and a local fake Messages API; **not yet run against the live API** (no key in this environment) | Adaptive thinking, effort `low`, server-side refusal fallback, cached system prompt, at most 6 model calls a message. |
| Help pages and feature registry | **Implemented** | 18 pages, 38 features, 15 screens in `doc/sw67/help`. Every feature is labelled AVAILABLE, PLANNED or UNAVAILABLE. Served at `/help/<id>`. |
| Account and owner tools | **Implemented, no data source** | Enforce scope, the resident's switch, own records only, and the owner list. Answer UNAVAILABLE until an Avatar Life records service exists. Development mode serves labelled mock data. |
| Support requests | **Implemented** | Drafted by King Jahmir, sent only on Confirm, stored for the owner (`GET /v1/owner/support_requests`). No email or ticket desk. |
| Preferences (optional memory) | **Implemented** | Off by default; proposals need Confirm; view, edit and delete in the panel. |
| Conversation history, deletion, retention | **Implemented** | Delete one or all (with a confirmation dialog); auto-delete after 30 days. |
| Viewer core (`sw67core/assistant`) | **Implemented** (9 new test cases; 73 total, all passing) | Reply rendering, protocol, error messages, unread count, retry state. |
| Viewer bubble and panel | **Implemented**, builds on Windows and Linux; seen in-world (bubble and Settings tab) | Crown bubble, conversation panel, settings tab, menu item **Me > 6\|7 Studio > King Jahmir**. Says "Not set up yet" until a server and sign-in are configured. |
| Voice | **UNAVAILABLE** | The viewer has no speech recognition or read-aloud engine. Text chat works on its own. |

## 3. What was verified, and how

Ran in this environment: `python -m unittest` (gateway, 37 tests) and
`sw67core_tests` (73 cases, 3039 assertions; the new source also compiles
clean under Clang with `-Wall -Wextra -Wpedantic -Wconversion -Wshadow
-Werror`). A local gateway in development mode answered real HTTP requests
with curl.

| Requirement | Verified? |
|---|---|
| 1. Bubble opens, moves, minimizes, closes | **No.** Code and layout written; needs a built viewer. Check list in section 5. |
| 2. Multi-turn conversation and follow-up questions | **Gateway, yes**: history (including thinking blocks) is carried between turns, follow-ups work in a scripted conversation. Live model behaviour not yet seen. |
| 3. App answers use actual documentation | **Retrieval, yes**: test questions hit the right page (workspaces, performance, photography, hygiene, furniture approval, RLV), and the model receives those sections. Live answers not yet seen. |
| 4. Planned features not presented as released | **Data and rules, yes**: the registry marks them; a test checks every non-available feature's help page says PLANNED or UNAVAILABLE; the prompt requires the labels. Live model not yet seen. |
| 5. Account tools enforce authentication and authorization | **Yes.** Missing, forged, expired, wrong-issuer and `alg: none` tokens are refused; account reads need scope and switch; owner tools need role and server list; other residents' conversations return 404. |
| 6. Sensitive actions need confirmation | **Yes.** Drafts do nothing until Confirm; a second Confirm does not repeat; cancelled drafts cannot be confirmed; there are no tools for purchases, deletion, sharing, RLV or roleplay settings, XP or approvals. |
| 7. Service failures show useful messages | **Yes** (gateway and viewer core): timeout, busy, unreachable, not configured, expired session, rate limited; each says whether Retry helps. A failed turn saves nothing, so Retry starts clean. |
| 8. Conversation deletion works | **Yes** (gateway): delete one, delete all with confirmation, repeats succeed, retention purge. |
| 9. Keyboard and colourblind-friendly controls | **Partly.** Status text always carries a symbol and a word (from the accessibility module); unread shows as "2 new". Tab order and Enter-to-send are in the layout but not yet tried in a viewer. |

## 4. Missing integrations and configuration

1. **Avatar Life does not exist yet.** No stats, levels, careers, activities,
   furniture registry or approval system was found in the repository, so
   King Jahmir labels all of it UNAVAILABLE and cannot explain anyone's
   stats. When it is built, point `KJ_ACCOUNT_API_URL` at it (contract in
   `services/king-jahmir/README.md`).
2. **No 6|7 account service.** Session tokens are issued by hand with
   `python -m kingjahmir issue-token` until the Milestone 5 account service
   issues them on sign-in. Residents paste the token into the panel's
   Settings tab.
3. **Hosting.** Planned on Vercel with the Supabase project "6|7 Back
   Office" for storage (steps in `services/king-jahmir/README.md`, "Host it on
   Vercel with Supabase"). It needs `ANTHROPIC_API_KEY`, `KJ_AUTH_SECRET`,
   `KJ_OWNER_IDS` (Jeff's agent UUID), `KJ_DATABASE_URL` and `CRON_SECRET`.
   Then set the viewer's gateway address (Settings tab, or
   `SW67KingJahmirGatewayURL`).
4. **Not yet run against the live Claude API.** First run: ask the six
   starter questions and check the answers cite help pages and use the
   status labels.
5. **Support desk.** Confirmed support requests wait in the owner's queue;
   no email or ticket system is connected.
6. **Entitlements** are not enforced (no billing service), as for every 6|7
   feature.

## 5. In-world checks (once a build exists)

1. After login the gold crown bubble appears at the right edge above the
   bottom toolbar. Drag it by its gold top strip; log out and in, and it is
   where you left it.
2. Click the crown: the panel opens. Click again: it minimizes. Close it with
   the X; the conversation is still there when reopened.
3. With the panel closed, let an answer arrive: the bubble reads "1 new".
4. Press **Help me build furniture**; answer his follow-up; check the reply
   labels approval UNAVAILABLE and links the help page.
5. Ask "open workspace profiles": an **Open: Workspace Profiles** button
   appears and works; the gold link in the text works too.
6. Ask him to file a support request; Cancel, then ask again and Confirm.
   The owner sees one request.
7. Stop the gateway and send a message: "can't be reached" with **Retry**.
   Start it and press Retry: one answer, not two.
8. **Delete conversation** and **Delete all** ask first, then empty the
   history list.
9. With Deuteranopia selected in Accessibility, statuses still read by symbol
   and word. Use only Tab, Enter and Esc for a whole conversation. Try
   **A-** and **A+**.
