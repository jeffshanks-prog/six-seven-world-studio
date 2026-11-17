"""One conversation turn: the user's message in, King Jahmir's reply out.

A manual tool loop over the Messages API, so that every tool call runs here
with the caller's identity and the server's checks, and so that the loop is
bounded. History is append-only (thinking blocks are passed back as the API
returned them) and is saved only when the whole turn succeeds, so a retry
starts from the same state.
"""

from __future__ import annotations

import logging
import re

import anthropic

from . import prompt
from .render import sanitize
from .tools import ToolContext, run_tool, tool_defs

log = logging.getLogger("kingjahmir")

FALLBACK_BETA = "server-side-fallback-2026-07-01"
_SCREEN_ID = re.compile(r"^[a-z0-9_]{1,64}$")


class AssistantError(Exception):
    def __init__(self, code: str, message: str, status: int, retryable: bool, retry_after: float | None = None):
        super().__init__(message)
        self.code, self.message, self.status = code, message, status
        self.retryable, self.retry_after = retryable, retry_after


def _block_dict(block) -> dict:
    if isinstance(block, dict):
        return block
    return block.to_dict()


def _context_block(ctx: ToolContext, screen: dict | None, preferences: dict[str, str]) -> str | None:
    lines = []
    if ctx.flags.contextual and screen:
        sid = screen.get("screen", "")
        if isinstance(sid, str) and _SCREEN_ID.match(sid):
            known = ctx.kb.screens.get(sid)
            title = known["title"] if known else "(not a 6|7 registered window)"
            lines.append(f"current_screen: {sid} ({title})")
            feature = screen.get("feature", "")
            if isinstance(feature, str) and feature in ctx.kb.features:
                lines.append(f"selected_feature: {feature}")
    if ctx.viewer_version:
        lines.append(f"viewer_version: {ctx.viewer_version}")
    lines.append(f"live_account_help: {'on' if ctx.flags.account_help else 'off'}")
    lines.append(f"remember_preferences: {'on' if ctx.flags.memory else 'off'}")
    if ctx.principal.owner:
        lines.append("caller_role: owner")
    if ctx.dev_mode:
        lines.append("development_mode: on (any mock data is labelled as such)")
    if ctx.flags.memory and preferences:
        lines.append("saved_preferences:")
        lines.extend(f"  {k}: {v}" for k, v in preferences.items())
    return "<viewer_context>\n" + "\n".join(lines) + "\n</viewer_context>"


class Assistant:
    def __init__(self, cfg, client=None):
        self.cfg = cfg
        self.client = client
        self.tools = tool_defs(cfg.web_search)
        self.system = [{"type": "text", "text": prompt.SYSTEM_PROMPT, "cache_control": {"type": "ephemeral"}}]

    def _call(self, messages: list[dict]):
        if self.client is None:
            raise AssistantError("ai_not_configured", "King Jahmir's AI service is not set up on this server.",
                                 503, False)
        try:
            return self.client.beta.messages.create(
                model=self.cfg.model,
                max_tokens=self.cfg.max_tokens,
                system=self.system,
                tools=self.tools,
                messages=messages,
                thinking={"type": "adaptive"},
                output_config={"effort": self.cfg.effort},
                betas=[FALLBACK_BETA],
                fallbacks="default",
            )
        except anthropic.APITimeoutError as e:
            raise AssistantError("ai_timeout", "King Jahmir took too long to answer.", 504, True) from e
        except anthropic.APIConnectionError as e:
            raise AssistantError("ai_unreachable", "King Jahmir's AI service could not be reached.", 503, True) from e
        except anthropic.RateLimitError as e:
            ra = None
            try:
                ra = float(e.response.headers.get("retry-after", "20"))
            except (TypeError, ValueError):
                ra = 20.0
            raise AssistantError("ai_busy", "King Jahmir is busy right now.", 503, True, ra) from e
        except (anthropic.AuthenticationError, anthropic.PermissionDeniedError) as e:
            raise AssistantError("ai_not_configured", "King Jahmir's AI service is not set up correctly.",
                                 503, False) from e
        except anthropic.BadRequestError as e:
            log.warning("model rejected request: %s", getattr(e, "message", "")[:200])
            raise AssistantError("ai_error", "King Jahmir could not answer that.", 502, False) from e
        except anthropic.APIStatusError as e:
            retry = e.status_code >= 500 or e.status_code == 529
            raise AssistantError("ai_error", "King Jahmir's AI service had a problem.", 503, retry) from e

    def turn(self, ctx: ToolContext, history: list[dict], user_text: str, screen: dict | None,
             preferences: dict[str, str]) -> dict:
        """Returns {"reply", "links", "events", "new_messages": [(role, display, api_msg)]}."""
        content = [{"type": "text", "text": user_text}]
        cblock = _context_block(ctx, screen, preferences)
        if cblock:
            content.append({"type": "text", "text": cblock})
        user_msg = {"role": "user", "content": content}
        new: list[tuple[str, str | None, dict]] = [("user", user_text, user_msg)]
        messages = list(history) + [user_msg]

        final_text = ""
        for _round in range(self.cfg.max_tool_rounds):
            resp = self._call(messages)
            assistant_msg = {"role": "assistant", "content": [_block_dict(b) for b in resp.content]}
            messages.append(assistant_msg)
            text = "".join(b.get("text", "") for b in assistant_msg["content"] if b.get("type") == "text")

            if resp.stop_reason == "refusal":
                final_text = prompt.REFUSAL_TEXT
                new.append(("assistant", final_text, assistant_msg))
                break
            if resp.stop_reason == "pause_turn":
                new.append(("assistant", None, assistant_msg))
                continue
            tool_uses = [b for b in assistant_msg["content"] if b.get("type") == "tool_use"]
            if resp.stop_reason == "tool_use" and tool_uses:
                new.append(("assistant", None, assistant_msg))
                results = []
                for tu in tool_uses:
                    out, is_err = run_tool(tu.get("name", ""), tu.get("input"), ctx)
                    r = {"type": "tool_result", "tool_use_id": tu["id"], "content": out}
                    if is_err:
                        r["is_error"] = True
                    results.append(r)
                tool_msg = {"role": "user", "content": results}
                messages.append(tool_msg)
                new.append(("tool", None, tool_msg))
                continue
            if resp.stop_reason == "max_tokens" and not text:
                text = "My answer was cut short. Ask me to continue, or ask a narrower question."
            final_text = text.strip() or "I don't have an answer for that. Could you tell me a bit more?"
            new.append(("assistant", final_text, assistant_msg))
            break
        else:
            final_text = prompt.ROUND_LIMIT_TEXT
            # History must stay valid: end the turn with an assistant message.
            new.append(("assistant", final_text,
                        {"role": "assistant", "content": [{"type": "text", "text": final_text}]}))

        reply, links = sanitize(final_text, ctx.kb)
        # Keep the stored display text the same as what was shown.
        role, _, msg = new[-1]
        new[-1] = (role, reply, msg)
        return {"reply": reply, "links": links, "events": ctx.events, "new_messages": new}


def make_client(cfg):
    """The real Anthropic client, or None when no key is configured."""
    if cfg.dev_mock_model:
        from .devmodel import DevMockClient
        return DevMockClient()
    try:
        return anthropic.Anthropic(timeout=cfg.model_timeout, max_retries=2)
    except anthropic.AnthropicError:
        return None


def history_ok(messages: list[dict]) -> bool:
    """A stored history the API will accept: starts with a user message."""
    return not messages or messages[0].get("role") == "user"

