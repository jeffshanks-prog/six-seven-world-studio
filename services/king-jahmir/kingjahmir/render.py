"""Makes a model reply safe to show. The viewer renders replies as plain text
(numbered and bulleted lists, **bold**) and turns only two kinds of link into
something clickable: [Title](screen:<id>) for a registered app window and
[Title](help:<id>) for a help page. Any other link becomes plain text."""

from __future__ import annotations

import re

from .knowledge import KnowledgeBase

MAX_REPLY = 8000
_CONTROL = re.compile(r"[\x00-\x08\x0b\x0c\x0e-\x1f\x7f‪-‮⁦-⁩]")
_LINK = re.compile(r"\[([^\[\]\n]{1,100})\]\(([^()\s]{1,400})\)")


def sanitize(text: str, kb: KnowledgeBase) -> tuple[str, list[dict]]:
    text = _CONTROL.sub("", text or "").strip()
    if len(text) > MAX_REPLY:
        text = text[:MAX_REPLY].rstrip() + " ..."
    links: list[dict] = []

    def repl(m: re.Match) -> str:
        label, target = m.group(1).strip(), m.group(2)
        kind, _, ident = target.partition(":")
        if kind == "screen" and ident in kb.screens:
            links.append({"type": "screen", "id": ident, "label": label})
            return f"[{label}](screen:{ident})"
        if kind == "help" and ident in kb.pages:
            links.append({"type": "help", "id": ident, "label": label, "url": kb.help_url(ident)})
            return f"[{label}](help:{ident})"
        if kind in ("http", "https"):
            return f"{label} ({target})"
        return label

    text = _LINK.sub(repl, text)
    # Anything still shaped like a link (odd brackets, nested parentheses) is
    # broken apart so no renderer can read it as one.
    text = re.sub(r"\]\((?!(?:screen|help):[a-z0-9_-]+\))", "] (", text)
    return text, links
