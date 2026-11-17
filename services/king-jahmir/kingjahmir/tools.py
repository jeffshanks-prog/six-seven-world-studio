"""The tools King Jahmir may call. Every call is validated here, on the server,
whatever the model sent: types, lengths, enums, and the caller's permissions.

Documentation tools (search_help, get_feature_status, list_features,
open_app_screen) are separate from permission-controlled account tools
(read_my_account, owner_records). Tools that would change something only
create a pending action the user must confirm in the app.
"""

from __future__ import annotations

import json
import re
from dataclasses import dataclass, field

from .account import OWNER_SECTIONS, USER_SECTIONS, AccountProvider, Unavailable
from .auth import Principal
from .knowledge import KnowledgeBase
from .store import Store

SUPPORT_CATEGORIES = ("bug", "billing", "account", "feature_question", "creator_approval", "safety", "other")
PREF_KEY = re.compile(r"^[a-z][a-z0-9_]{0,39}$")
MAX_PREFERENCES = 20


def _schema(props: dict) -> dict:
    return {"type": "object", "properties": props, "required": list(props), "additionalProperties": False}


TOOL_DEFS = [
    {
        "name": "search_help",
        "description": "Search the 6|7 World Studio help pages. Returns matching sections with their page id. "
                       "Use before describing how any app feature, window, menu or button works.",
        "input_schema": _schema({"query": {"type": "string", "description": "What to look up, in plain words."}}),
    },
    {
        "name": "get_feature_status",
        "description": "Look up one feature in the feature registry: AVAILABLE, PLANNED or UNAVAILABLE, its plan, "
                       "window and help page. Use whenever availability matters.",
        "input_schema": _schema({"feature": {"type": "string", "description": "Feature id or name."}}),
    },
    {
        "name": "list_features",
        "description": "List registry features with their status, optionally for one area. Use for "
                       "'What can I do here?'.",
        "input_schema": _schema({"area": {"type": "string", "enum": [
            "all", "account", "accessibility", "ai", "avatar", "avatar_life", "communication", "creation",
            "events", "inventory", "performance", "roleplay", "settings", "support", "workspace"]}}),
    },
    {
        "name": "open_app_screen",
        "description": "Offer the user a button that opens an app window. The window opens only when the user "
                       "clicks it. Use when the user asks to open or go to a window.",
        "input_schema": _schema({"screen_id": {"type": "string", "description": "A screen id from the registry."}}),
    },
    {
        "name": "read_my_account",
        "description": "Read the signed-in user's own Avatar Life records: stats, level and XP, careers and "
                       "skills, active activities and cooldowns, recent activity results, or entitlements. "
                       "The server checks permission and the user's Live account help switch.",
        "input_schema": _schema({"section": {"type": "string", "enum": list(USER_SECTIONS)}}),
    },
    {
        "name": "owner_records",
        "description": "Owner only, read-only: the product approval queue, registered products, suspensions, "
                       "or audit history. The server refuses anyone who is not the owner.",
        "input_schema": _schema({"section": {"type": "string", "enum": list(OWNER_SECTIONS)}}),
    },
    {
        "name": "prepare_support_request",
        "description": "Draft a support request and show it to the user for review. Nothing is sent until the "
                       "user presses Confirm. Never include passwords or other residents' private information.",
        "input_schema": _schema({
            "category": {"type": "string", "enum": list(SUPPORT_CATEGORIES)},
            "summary": {"type": "string", "description": "One line, under 120 characters."},
            "details": {"type": "string", "description": "What happened, steps tried, what the user expected."},
        }),
    },
    {
        "name": "propose_preference",
        "description": "Propose saving a preference the user stated (for example answer_length = short). "
                       "Only works when the user turned on Remember my preferences; the user must confirm.",
        "input_schema": _schema({
            "key": {"type": "string", "description": "lower_snake_case name, e.g. answer_length"},
            "value": {"type": "string"},
        }),
    },
]
for _t in TOOL_DEFS:
    _t["strict"] = True

WEB_SEARCH_TOOL = {"type": "web_search_20260209", "name": "web_search", "max_uses": 3}


@dataclass
class Flags:
    contextual: bool = False
    account_help: bool = False
    memory: bool = False


@dataclass
class ToolContext:
    principal: Principal
    flags: Flags
    conversation_id: str
    viewer_version: str
    store: Store
    kb: KnowledgeBase
    provider: AccountProvider
    dev_mode: bool
    events: list = field(default_factory=list)
    created_actions: list = field(default_factory=list)


class ToolInputError(ValueError):
    pass


def _str(inp: dict, key: str, max_len: int, required: bool = True) -> str:
    v = inp.get(key)
    if not isinstance(v, str) or (required and not v.strip()):
        raise ToolInputError(f"'{key}' must be a non-empty string")
    v = v.strip()
    if len(v) > max_len:
        raise ToolInputError(f"'{key}' is longer than {max_len} characters")
    return v


def _enum(inp: dict, key: str, allowed) -> str:
    v = inp.get(key)
    if v not in allowed:
        raise ToolInputError(f"'{key}' must be one of {', '.join(allowed)}")
    return v


def run_tool(name: str, inp, ctx: ToolContext) -> tuple[str, bool]:
    """Runs one tool call. Returns (JSON text for the model, is_error)."""
    if not isinstance(inp, dict):
        return json.dumps({"error": "input must be an object"}), True
    try:
        fn = _TOOLS.get(name)
        if fn is None:
            return json.dumps({"error": f"unknown tool {name}"}), True
        result = fn(inp, ctx)
        return json.dumps(result, ensure_ascii=False), False
    except ToolInputError as e:
        return json.dumps({"error": f"invalid input: {e}"}), True


def _search_help(inp, ctx: ToolContext) -> dict:
    q = _str(inp, "query", 300)
    hits = ctx.kb.search(q, ctx.viewer_version)
    return {"untrusted_reference_material": True, "query": q, "results": hits,
            "note": "No help page matched." if not hits else ""}


def _feature_status(inp, ctx: ToolContext) -> dict:
    key = _str(inp, "feature", 120)
    f = ctx.kb.feature(key)
    if not f:
        return {"found": False, "note": "No registry entry matches. Treat this feature as UNAVAILABLE and say the "
                                       "information is missing."}
    return {"found": True, "feature": ctx.kb.describe_feature(f, ctx.viewer_version)}


def _list_features(inp, ctx: ToolContext) -> dict:
    area = inp.get("area", "all")
    if area != "all" and area not in ctx.kb.areas():
        raise ToolInputError("unknown area")
    return {"area": area, "features": ctx.kb.list_features(area)}


def _open_screen(inp, ctx: ToolContext) -> dict:
    sid = _str(inp, "screen_id", 64)
    screen = ctx.kb.screens.get(sid)
    if not screen:
        return {"offered": False, "error": "Unknown screen id. Use an id from get_feature_status.",
                "known_screens": sorted(ctx.kb.screens)}
    if not any(e.get("type") == "open_screen" and e["screen"] == sid for e in ctx.events):
        ctx.events.append({"type": "open_screen", "screen": sid, "title": screen["title"]})
    return {"offered": True, "screen": screen,
            "note": "An Open button is shown to the user. The window opens only if they click it."}


def _read_my_account(inp, ctx: ToolContext) -> dict:
    section = _enum(inp, "section", USER_SECTIONS)
    if not ctx.principal.can("account:read"):
        return {"status": "denied", "reason": "This 6|7 session is not allowed to read account records."}
    if not ctx.flags.account_help:
        return {"status": "denied",
                "reason": "Live account help is off. The user can turn it on in the King Jahmir panel."}
    try:
        data = ctx.provider.user_section(ctx.principal.sub, section)
    except Unavailable as e:
        return {"status": "UNAVAILABLE", "section": section, "reason": e.reason}
    return {"status": "ok", "section": section, "records": data,
            "note": "Server records for the signed-in user only. Explain them; do not add to them."}


def _owner_records(inp, ctx: ToolContext) -> dict:
    section = _enum(inp, "section", OWNER_SECTIONS)
    if not ctx.principal.owner:
        return {"status": "denied", "reason": "Owner records are only for the 6|7 owner."}
    try:
        data = ctx.provider.owner_section(section)
    except Unavailable as e:
        return {"status": "UNAVAILABLE", "section": section, "reason": e.reason}
    return {"status": "ok", "section": section, "records": data, "read_only": True}


def _prepare_support(inp, ctx: ToolContext) -> dict:
    payload = {
        "category": _enum(inp, "category", SUPPORT_CATEGORIES),
        "summary": _str(inp, "summary", 120),
        "details": _str(inp, "details", 2000),
        "viewer_version": ctx.viewer_version[:40],
    }
    aid = ctx.store.add_action(ctx.principal.sub, ctx.conversation_id, "support_request", payload)
    ctx.created_actions.append(aid)
    ctx.events.append({"type": "confirm_action", "action_id": aid, "kind": "support_request",
                       "title": "Send this support request?",
                       "preview": f"[{payload['category']}] {payload['summary']}\n\n{payload['details']}"})
    return {"status": "draft_shown_to_user", "action_id": aid,
            "note": "Not sent. The user must press Confirm in the app."}


def _propose_preference(inp, ctx: ToolContext) -> dict:
    if not ctx.flags.memory:
        return {"status": "denied", "reason": "Remember my preferences is off; nothing can be saved."}
    key = _str(inp, "key", 40)
    if not PREF_KEY.match(key):
        raise ToolInputError("key must be lower_snake_case")
    value = _str(inp, "value", 200)
    if ctx.store.count_preferences(ctx.principal.sub) >= MAX_PREFERENCES and \
            key not in ctx.store.preferences(ctx.principal.sub):
        return {"status": "denied", "reason": f"At most {MAX_PREFERENCES} preferences can be saved."}
    aid = ctx.store.add_action(ctx.principal.sub, ctx.conversation_id, "preference", {"key": key, "value": value})
    ctx.created_actions.append(aid)
    ctx.events.append({"type": "confirm_action", "action_id": aid, "kind": "preference",
                       "title": "Save this preference?", "preview": f"{key}: {value}"})
    return {"status": "proposal_shown_to_user", "action_id": aid, "note": "Not saved until the user confirms."}


_TOOLS = {
    "search_help": _search_help,
    "get_feature_status": _feature_status,
    "list_features": _list_features,
    "open_app_screen": _open_screen,
    "read_my_account": _read_my_account,
    "owner_records": _owner_records,
    "prepare_support_request": _prepare_support,
    "propose_preference": _propose_preference,
}


def tool_defs(web_search: bool) -> list[dict]:
    defs = [dict(t) for t in TOOL_DEFS]
    if web_search:
        defs.append(dict(WEB_SEARCH_TOOL))
    return defs


def confirm_action(store: Store, sub: str, aid: str) -> dict:
    """Carries out a pending action the user confirmed. Safe to repeat: a
    second confirm returns the first result and does nothing again."""
    action = store.get_action(sub, aid)
    if action is None:
        return {"status": "not_found"}
    if action["status"] == "confirmed":
        return {"status": "confirmed", "result": action["result"], "repeated": True}
    if action["status"] != "pending":
        return {"status": action["status"]}
    p = action["payload"]
    if action["kind"] == "support_request":
        # Claim the action first so two concurrent confirms cannot both send.
        if not store.resolve_action(sub, aid, "confirmed", {"pending": True}):
            return confirm_action(store, sub, aid)
        sid = store.add_support_request(sub, p)
        result = {"support_request_id": sid, "message": "Your support request was sent to the 6|7 owner's queue."}
    elif action["kind"] == "preference":
        if not store.resolve_action(sub, aid, "confirmed", {"pending": True}):
            return confirm_action(store, sub, aid)
        store.set_preference(sub, p["key"], p["value"])
        result = {"message": f"Saved preference {p['key']}."}
    else:
        return {"status": "unsupported"}
    store.set_action_result(aid, result)  # what a repeated confirm returns
    return {"status": "confirmed", "result": result}
