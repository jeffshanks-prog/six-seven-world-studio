# King Jahmir gateway

The backend for King Jahmir, the 6|7 World Studio AI guide. The viewer never
holds an AI key: it sends the resident's message and their 6|7 session token
here, and this gateway:

1. checks the token (signature, issuer, expiry, scopes, owner list);
2. applies per-resident and per-address rate limits, a daily cap, a size
   limit, and one message at a time per resident;
3. replays the stored answer when the viewer retries with the same
   `Idempotency-Key`, so a retry never runs a turn or an action twice;
4. calls Claude (`claude-opus-5-5` by default) with King Jahmir's system prompt
   and tools, in a bounded tool loop;
5. runs every tool call itself, validating the input and the caller's
   permissions on the server;
6. makes the reply safe to show (control characters removed, only
   `screen:` and `help:` links kept) and stores the conversation for the
   retention period.

Documentation and account data are separate tools. Documentation comes from
`doc/sw67/help/` (help pages plus `features.json`, the feature registry).
Account and owner records come from an account provider, and no Avatar Life
records service exists yet, so those tools answer **UNAVAILABLE** (see
[Account records](#account-records)).

## Run it

Python 3.11 or later.

```
cd services/king-jahmir
python -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
export KJ_AUTH_SECRET="$(python -c 'import secrets; print(secrets.token_urlsafe(48))')"
export ANTHROPIC_API_KEY=...            # server-side only, never in the viewer
export KJ_OWNER_IDS=<Jeff's SL agent UUID>
python -m kingjahmir check-docs         # validates the help pages and registry
python -m kingjahmir serve              # listens on 127.0.0.1:8767
```

Put it behind a TLS reverse proxy (Caddy, nginx) and give the viewer the
https address. The viewer refuses plain http except to `127.0.0.1` and
`localhost`. Or host it on Vercel with a Supabase database, below.

Tests (no network, no key; they use a scripted model and the real Anthropic
SDK pointed at a local fake API):

```
python -m unittest -v
```

Tests on PostgreSQL as well (as on Vercel), against any empty database:

```
KJ_TEST_DATABASE_URL=postgresql://postgres@127.0.0.1:5432/kj_test python -m unittest
```

## Host it on Vercel with Supabase

The gateway also runs as one Vercel Python function (`api/index.py`, routed by
`vercel.json`), keeping everything it shares between requests in PostgreSQL:
conversations, retries, drafts, preferences, support requests, rate limits and
the one-message-at-a-time marks. A Vercel cron job calls `/cron/purge` daily
for the retention clean-up. The help pages travel with it in
`kingjahmir/help` (refresh with `python -m kingjahmir bundle-docs` after
editing `doc/sw67/help`; a test fails while the copy is stale).

1. **Database.** In the Supabase project, open **Connect** and copy the
   **Transaction pooler** connection string (port 6543) and put the database
   password in it. The gateway creates its own tables (all named `kj_...`) on
   first start, with row level security on and no policies, so Supabase's
   Data API (the project's public key) can read none of them.
2. **Project.** In Vercel, import the GitHub repository as a new project and
   set **Root Directory** to `services/king-jahmir`. No framework, no build
   command.
3. **Environment variables** (Project Settings > Environment Variables):
   `KJ_DATABASE_URL` (step 1), `ANTHROPIC_API_KEY`, `KJ_AUTH_SECRET`,
   `KJ_OWNER_IDS`, and `CRON_SECRET` (any long random string; Vercel sends it
   to the cron job). `KJ_PUBLIC_URL` defaults to the project's production
   address.
4. **Deploy**, then check `https://<project>.vercel.app/healthz` answers
   `"ok": true` and `"ai_client": true`.
5. Get a sign-in: with `KJ_SETUP_CODE` set, open
   `https://<project>.vercel.app/setup`, enter the code and your avatar key,
   and copy the token (or run `python -m kingjahmir issue-token` with the same
   `KJ_AUTH_SECRET`).
6. In the viewer: **King Jahmir > Settings & privacy**, paste
   `https://<project>.vercel.app` as the server and Save, then paste the token
   under **6|7 sign-in** and press **Sign in**.

Vercel's function time limit is set to 300 seconds in `vercel.json`, longer
than the slowest turn (6 model calls of at most 60 seconds each is the
theoretical worst; turns usually take seconds).

### Development mode, no AI cost

```
export KJ_DEV_MODE=1 KJ_DEV_MOCK_MODEL=1 KJ_AUTH_SECRET=<32+ chars>
python -m kingjahmir serve
python -m kingjahmir issue-token <agent-uuid> --dev --account
```

`KJ_DEV_MOCK_MODEL` replaces Claude with a stand-in that only looks things
up with the real tools. Every reply starts with
`[DEVELOPMENT MOCK MODEL - not the real King Jahmir AI]`, account tools
return records labelled `DEVELOPMENT MOCK DATA - not real records`, and the
viewer shows "DEVELOPMENT MODE" in the panel. Development tokens (`--dev`)
are refused when `KJ_DEV_MODE` is off.

## Environment variables

| Variable | Default | Meaning |
|---|---|---|
| `ANTHROPIC_API_KEY` | (none) | Claude API key. Without credentials, chat answers `ai_not_configured`. |
| `KJ_AUTH_SECRET` | **required** | At least 32 bytes. Signs and checks 6\|7 session tokens (HS256). |
| `KJ_AUTH_ISSUER` | `sw67-accounts` | Expected `iss` claim. |
| `KJ_OWNER_IDS` | (none) | Comma-separated agent UUIDs allowed owner tools. A token needs the `owner` role **and** to be on this list. |
| `KJ_HOST`, `KJ_PORT` | `127.0.0.1`, `8767` | Listen address. |
| `KJ_PUBLIC_URL` | `http://KJ_HOST:KJ_PORT` | The https address residents reach; used for help-page links. |
| `KJ_DB_PATH` | `kingjahmir.sqlite3` | SQLite file for conversations, retries, drafts, preferences, support requests, rate limits. Used when `KJ_DATABASE_URL` is not set. |
| `KJ_DATABASE_URL` | (none) | PostgreSQL connection string (`postgresql://...`) instead of SQLite. Required on Vercel; with Supabase use the transaction pooler (port 6543). |
| `KJ_SETUP_CODE` | (none) | At least 16 characters. Turns on `/setup`, a page where an owner (an id in `KJ_OWNER_IDS`) enters this code and gets a 30-day sign-in token, with no command line. Tries are limited per address. Remove it to turn the page off. |
| `CRON_SECRET` | (none) | Lets `GET /cron/purge` with `Authorization: Bearer <it>` run the retention clean-up (Vercel Cron). Without it the route refuses. |
| `KJ_DOCS_DIR` | `doc/sw67/help` in this repo, else the copy in `kingjahmir/help` | Help pages and feature registry. |
| `KJ_MODEL` | `claude-opus-5-5` | Claude model. |
| `KJ_EFFORT` | `low` | `low`, `medium`, `high`, `xhigh` or `max`. Low suits chat; raise it if answers fall short. |
| `KJ_MAX_TOKENS` | `4096` | Output cap per model call. |
| `KJ_MODEL_TIMEOUT_SECONDS` | `60` | Per model call; the SDK retries twice. |
| `KJ_MAX_TOOL_ROUNDS` | `6` | Model calls per message at most. |
| `KJ_WEB_SEARCH` | off | `1` adds Anthropic's web search tool (max 3 uses a message) for current external facts. Costs extra. |
| `KJ_RATE_PER_MINUTE`, `KJ_RATE_BURST` | `8`, `4` | Messages per resident per minute, and burst. The per-address limit is 4x. |
| `KJ_DAILY_MESSAGES` | `200` | Messages per resident per day. |
| `KJ_RETENTION_DAYS` | `30` | Conversations untouched this long are deleted (hourly by `serve`, daily by the Vercel cron job). |
| `KJ_MAX_MESSAGE_CHARS` | `4000` | Longest message. |
| `KJ_MAX_TURNS` | `60` | Messages per conversation before "start a new conversation". |
| `KJ_ACCOUNT_API_URL`, `KJ_ACCOUNT_API_TOKEN` | (none) | The Avatar Life records service, once it exists (https only). |
| `KJ_TRUST_FORWARDED_FOR` | off (on when `VERCEL` is set) | `1` behind a reverse proxy, so per-address limits use `X-Forwarded-For`. |
| `KJ_DEV_MODE`, `KJ_DEV_MOCK_MODEL` | off | Development only; see above. |

Logs are one JSON line per request: route (ids masked), status, time, and a
12-character hash of the resident id. No message text, tokens or keys are
logged.

## Sessions and sign-in

There is no 6|7 account service yet (billing and accounts are Milestone 5),
so nothing can issue tokens on a resident's sign-in. Until it exists, whoever
holds `KJ_AUTH_SECRET` issues tokens:

```
python -m kingjahmir issue-token <agent-uuid> [--account] [--owner] [--hours 12]
```

The resident pastes the token into **King Jahmir > Settings & privacy > 6|7
sign-in**; the viewer keeps it in its protected data store. When the account
service exists it should issue the same HS256 token (claims in
`kingjahmir/auth.py`) after sign-in, and hand it to the viewer, so neither
the viewer nor the gateway changes. Scopes: `chat` (talk to King Jahmir),
`account:read` (read own records). Role: `owner`.

## HTTP API

All `/v1` routes need `Authorization: Bearer <token>`. Errors are
`{"error": {"code", "message", "retryable", "retry_after"?}}`.

| Route | |
|---|---|
| `GET /healthz` | Liveness (no auth). |
| `GET /setup`, `POST /setup` | Owner sign-in page, only with `KJ_SETUP_CODE` set. |
| `GET /cron/purge` | Retention clean-up; needs `Authorization: Bearer $CRON_SECRET`. |
| `GET /help/`, `GET /help/<id>` | Help pages as plain HTML (no auth). |
| `GET /v1/bootstrap` | Greeting, starter buttons, development mode, retention days. |
| `POST /v1/chat` | `Idempotency-Key` header required. Body: `message`, optional `conversation_id`, `flags` (`contextual`, `account_help`, `memory`), `context` (`screen`, `feature`), `client.viewer_version`. Answer: `conversation_id`, `reply`, `links`, `actions`. |
| `GET /v1/conversations`, `GET /v1/conversations/<id>` | The caller's own only. |
| `POST /v1/conversations/<id>/delete` (or `DELETE`) | Deletes it now. Repeats succeed. |
| `POST /v1/conversations/delete_all` | Body `{"confirm": true}`. |
| `POST /v1/actions/<id>/confirm`, `/cancel` | Carries out or drops a draft the model prepared. A second confirm returns the first result. |
| `GET /v1/preferences`, `POST /v1/preferences/<key>`, `POST /v1/preferences/<key>/delete` | View, edit, delete saved preferences. |
| `GET /v1/owner/support_requests` | Owner only. |
| `POST /v1/draft/lsl` | A draft LSL script for the Creator Workbench (text only, never compiled or run). |
| `POST /v1/phone/pair` | A one-use, 10-minute code that pairs a phone with the caller's events. |
| `POST /v1/phone/events` | Sends or updates an event on the Phone Companion: `id`, `title`, `starts` (epoch seconds), `minutes`, `location`, `details`, `host_name`, `shots`. |
| `GET /v1/phone/events/<id>/rsvps` | Its RSVPs, in the `sw67.rsvps` format Party Contacts imports (`verified: false`). |
| `POST /v1/phone/events/<id>/rsvp_link` | Opens (`{"open": true}`) or closes its public RSVP page and returns the link. The page shows the name, time, place, countdown and host name, plus a calendar file (`/r/<code>/calendar`); never the host's notes. |
| `POST /v1/phone/events/<id>/delete` | Takes it off the phone, with its team, chat and RSVPs. |
| `GET /phone`, `/phone/e/<id>`, `/phone/join/<invite>`, `/r/<code>` | The Phone Companion's pages: HTML forms, no scripts, a signed `SameSite=Strict` cookie, every post checked for its Origin. |

### Phone Companion

`kingjahmir/phone.py`. Its tables (`kj_phone_*`) sit in the same database as
King Jahmir's (`KJ_DATABASE_URL`), never in the 6|7 Back Office. No new
settings are needed. No Second Life credential reaches it: the host pairs a
phone with a code from their signed-in viewer, team members join from a
one-use invite link the host sends, and guests answer an RSVP page the host
opens and closes. Team chat follows `KJ_RETENTION_DAYS`; an event and
everything with it is deleted 30 days after it ends (the daily
`/cron/purge`).

## Tools King Jahmir has

| Tool | Kind | Rule |
|---|---|---|
| `search_help` | documentation | Help-page sections, marked as untrusted reference material. |
| `get_feature_status`, `list_features` | documentation | AVAILABLE / PLANNED / UNAVAILABLE from the registry, with plan, window and help page. |
| `open_app_screen` | navigation | Only registry screens. The viewer shows an Open button; nothing opens until the resident clicks. |
| `read_my_account` | account, read-only | Needs the `account:read` scope **and** the resident's Live account help switch; only the token's own `sub`. |
| `owner_records` | owner, read-only | Approval queue, products, suspensions, audit. Owner role plus `KJ_OWNER_IDS`. |
| `prepare_support_request` | draft | Shown to the resident; stored for the owner only after Confirm. |
| `propose_preference` | draft | Only with Remember my preferences on; saved only after Confirm. |

There is no tool that grants XP, stats, currency or privileges, approves or
registers a product, makes a purchase, deletes data, or changes RLV or
roleplay settings. Deleting conversations is the resident's own button, with
a confirmation dialog.

## Account records

`KJ_ACCOUNT_API_URL` names the Avatar Life records service when it exists.
The gateway calls, with `Authorization: Bearer KJ_ACCOUNT_API_TOKEN`:

- `GET /v1/residents/<agent-uuid>/<section>` where section is `stats`,
  `level`, `careers_skills`, `activities`, `activity_results` or
  `entitlements`;
- `GET /v1/owner/<section>` where section is `approval_queue`, `products`,
  `suspensions` or `audit`.

Each answers a JSON object under 64 KB, or 404 when there is no record. For
"why didn't my shower improve hygiene?", `activity_results` should say for
each recent session whether the product was approved, whether the session
completed, any cooldown, and the server-calculated result, so King Jahmir
explains from records rather than from the conversation.

## Scaling notes

Rate limits, retries and the one-message-at-a-time marks live in the
database, so any number of processes or serverless instances can share one
PostgreSQL database. SQLite suits a single `serve` process. Model calls are
the only slow part.
