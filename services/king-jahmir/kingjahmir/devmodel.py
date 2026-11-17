"""A stand-in for Claude in development mode only (KJ_DEV_MOCK_MODEL=1), so
the viewer panel and the tools can be exercised without an API key or cost.
It is not an AI: it looks things up with the real tools and says so in every
reply."""

from __future__ import annotations

import itertools
import json

LABEL = "[DEVELOPMENT MOCK MODEL - not the real King Jahmir AI]"
_ids = itertools.count(1)


class Block:
    def __init__(self, **kw):
        self.__dict__.update(kw)

    def to_dict(self) -> dict:
        return dict(self.__dict__)


class Response:
    def __init__(self, content, stop_reason):
        self.content = content
        self.stop_reason = stop_reason


def _last_user_text(messages) -> str:
    for m in reversed(messages):
        if m["role"] == "user":
            for b in m["content"]:
                if b.get("type") == "text" and not b["text"].startswith("<viewer_context>"):
                    return b["text"]
    return ""


class _Messages:
    def create(self, *, messages, **_kw):
        last = messages[-1]
        results = [b for b in last["content"] if b.get("type") == "tool_result"]
        if results:
            data = json.loads(results[0]["content"])
            if "results" in data:
                hits = data["results"]
                if not hits:
                    text = "I couldn't find a help page about that."
                else:
                    h = hits[0]
                    text = f"From the help page [{h['page_title']}](help:{h['page_id']}), {h['section']}:\n\n" \
                           f"{h['text'][:600]}"
            else:
                text = "The server answered:\n" + json.dumps(data, indent=1)[:800]
            return Response([Block(type="text", text=f"{LABEL}\n\n{text}")], "end_turn")
        q = _last_user_text(messages)
        lower = q.lower()
        if "stat" in lower or "level" in lower:
            name, inp = "read_my_account", {"section": "stats" if "stat" in lower else "level"}
        elif lower.startswith("open "):
            name, inp = "open_app_screen", {"screen_id": lower[5:].strip().replace(" ", "_")}
        elif "support" in lower:
            name, inp = "prepare_support_request", {"category": "other", "summary": q[:100], "details": q}
        else:
            name, inp = "search_help", {"query": q}
        return Response([Block(type="tool_use", id=f"toolu_dev{next(_ids)}", name=name, input=inp)], "tool_use")


class _Beta:
    messages = _Messages()


class DevMockClient:
    beta = _Beta()
