"""Help pages and the feature registry, loaded from doc/sw67/help.

Retrieval is lexical (BM25 over page sections): no embeddings service is
needed, results are explainable, and the corpus is small and maintained in
the repository. Retrieved text is handed to the model as untrusted
reference material, never as instructions.
"""

from __future__ import annotations

import json
import math
import re
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

STATUSES = ("AVAILABLE", "PLANNED", "UNAVAILABLE")
_WORD = re.compile(r"[a-z0-9]+")
_STOP = frozenset(
    "a an and are as at be by can do does for from how i if in is it its me my of on or so that the this "
    "to was what when where which who why will with you your".split()
)
_ID = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")


def tokens(text: str) -> list[str]:
    out = []
    for w in _WORD.findall(text.lower()):
        if w in _STOP:
            continue
        # Crude stemming so "profiles"/"profile" and "approved"/"approve" meet.
        for suf in ("ing", "es", "ed", "s"):
            if len(w) > len(suf) + 3 and w.endswith(suf):
                w = w[: -len(suf)]
                break
        out.append(w)
    return out


def parse_version(v: str) -> tuple[int, ...]:
    parts = []
    for p in (v or "").split(".")[:4]:
        m = re.match(r"\d+", p)
        parts.append(int(m.group()) if m else 0)
    return tuple(parts) or (0,)


@dataclass
class Section:
    doc_id: str
    title: str
    heading: str
    text: str
    terms: Counter = field(default_factory=Counter)
    length: int = 0


@dataclass
class Page:
    id: str
    title: str
    features: list[str]
    min_version: str
    body: str


class KnowledgeBase:
    def __init__(self, docs_dir: Path, public_url: str):
        self.public_url = public_url.rstrip("/")
        self.pages: dict[str, Page] = {}
        self.sections: list[Section] = []
        self.features: dict[str, dict] = {}
        self.screens: dict[str, dict] = {}
        self._load(Path(docs_dir))

    # ---- loading --------------------------------------------------------
    def _load(self, root: Path) -> None:
        reg = json.loads((root / "features.json").read_text(encoding="utf-8"))
        if reg.get("format") != "sw67-feature-registry":
            raise ValueError("features.json is not a feature registry")
        for f in reg["features"]:
            if f.get("status") not in STATUSES or not _ID.match(f.get("id", "")):
                raise ValueError(f"bad feature entry {f!r}")
            self.features[f["id"]] = f
        for s in reg.get("screens", []):
            if not _ID.match(s.get("id", "")):
                raise ValueError(f"bad screen entry {s!r}")
            self.screens[s["id"]] = s
        for path in sorted(root.glob("*.md")):
            if path.name == "README.md":
                continue
            page = self._parse_page(path.read_text(encoding="utf-8"), path.name)
            self.pages[page.id] = page
            self._index(page)
        for f in self.features.values():
            if f.get("help") and f["help"] not in self.pages:
                raise ValueError(f"feature {f['id']} names missing help page {f['help']}")
            if f.get("screen") and f["screen"] not in self.screens:
                raise ValueError(f"feature {f['id']} names unknown screen {f['screen']}")
        n = len(self.sections) or 1
        self._avg = sum(s.length for s in self.sections) / n
        df = Counter()
        for s in self.sections:
            df.update(set(s.terms))
        self._idf = {t: math.log(1 + (n - c + 0.5) / (c + 0.5)) for t, c in df.items()}

    @staticmethod
    def _parse_page(text: str, name: str) -> Page:
        m = re.match(r"^---\n(.*?)\n---\n(.*)$", text, re.S)
        if not m:
            raise ValueError(f"{name}: missing front matter")
        meta = {}
        for line in m.group(1).splitlines():
            k, _, v = line.partition(":")
            meta[k.strip()] = v.strip()
        pid = meta.get("id", "")
        if not _ID.match(pid):
            raise ValueError(f"{name}: bad id")
        return Page(id=pid, title=meta.get("title", pid),
                    features=[f.strip() for f in meta.get("features", "").split(",") if f.strip()],
                    min_version=meta.get("min_version", "0"), body=m.group(2).strip())

    def _index(self, page: Page) -> None:
        heading, buf = page.title, []

        def flush():
            body = "\n".join(buf).strip()
            if body:
                terms = Counter(tokens(page.title + " " + heading + " " + body))
                # Titles and headings count double.
                terms.update(tokens(page.title + " " + heading))
                self.sections.append(Section(page.id, page.title, heading, body, terms, sum(terms.values())))

        for line in page.body.splitlines():
            if line.startswith("#"):
                flush()
                buf = []
                heading = line.lstrip("#").strip()
            else:
                buf.append(line)
        flush()

    # ---- queries --------------------------------------------------------
    def help_url(self, page_id: str) -> str:
        return f"{self.public_url}/help/{page_id}"

    def search(self, query: str, viewer_version: str = "", k: int = 4) -> list[dict]:
        q = tokens(query)
        if not q:
            return []
        k1, b = 1.4, 0.75
        scored = []
        for s in self.sections:
            score = 0.0
            for t in q:
                tf = s.terms.get(t, 0)
                if tf:
                    score += self._idf.get(t, 0) * tf * (k1 + 1) / (tf + k1 * (1 - b + b * s.length / self._avg))
            if score > 0:
                scored.append((score, s))
        scored.sort(key=lambda x: -x[0])
        out, seen = [], set()
        for score, s in scored:
            key = (s.doc_id, s.heading)
            if key in seen:
                continue
            seen.add(key)
            page = self.pages[s.doc_id]
            item = {"page_id": s.doc_id, "page_title": s.title, "section": s.heading,
                    "text": s.text[:2400], "help_link": f"help:{s.doc_id}", "score": round(score, 2)}
            if viewer_version and parse_version(viewer_version) < parse_version(page.min_version):
                item["version_note"] = (f"This page applies to version {page.min_version} and later; "
                                        f"the user's viewer reports {viewer_version}.")
            out.append(item)
            if len(out) >= k:
                break
        return out

    def feature(self, key: str) -> dict | None:
        key = (key or "").strip().lower()
        if key in self.features:
            return self.features[key]
        q = set(tokens(key))
        if not q:
            return None
        best, best_score = None, 0
        for f in self.features.values():
            ft = set(tokens(f["id"].replace("_", " ") + " " + f["name"]))
            score = len(q & ft)
            if score > best_score:
                best, best_score = f, score
        return best

    def describe_feature(self, f: dict, viewer_version: str = "") -> dict:
        out = {"id": f["id"], "name": f["name"], "status": f["status"]}
        for k in ("plan", "milestone", "note"):
            if f.get(k):
                out[k] = f[k]
        if f["status"] == "AVAILABLE":
            out["development_build_only"] = bool(f.get("development"))
            if f.get("min_version"):
                out["min_version"] = f["min_version"]
                if viewer_version and parse_version(viewer_version) < parse_version(f["min_version"]):
                    out["in_users_version"] = False
        if f.get("screen"):
            s = self.screens[f["screen"]]
            out["screen"] = {"id": s["id"], "title": s["title"], "path": s["path"]}
        if f.get("help"):
            out["help_link"] = f"help:{f['help']}"
        out["entitlements"] = "Not enforced yet: there is no billing service, so every feature is open in development builds."
        return out

    def list_features(self, area: str = "all") -> list[dict]:
        return [{"id": f["id"], "name": f["name"], "status": f["status"]}
                for f in self.features.values() if area in ("all", f.get("area"))]

    def areas(self) -> list[str]:
        return sorted({f.get("area", "") for f in self.features.values() if f.get("area")})
