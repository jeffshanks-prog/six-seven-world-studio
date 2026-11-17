# 6|7 World Studio help pages

These pages are King Jahmir's documentation. The assistant answers app
questions only from these pages and from `features.json`, the feature
registry. Keep them true to the code:

- One page per topic. Front matter names its `id`, `title`, the registry
  `features` it covers, and the `min_version` it applies to.
- Use exact window, menu and button names as the viewer shows them.
- Say plainly when something is **PLANNED** or **UNAVAILABLE**. Never
  describe a feature as working before it ships.
- When a feature ships or changes, update its page and its entry in
  `features.json` in the same commit.

The gateway (`services/king-jahmir`) loads this folder at start-up and serves
each page at `/help/<id>`.
