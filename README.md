# 6|7 World Studio

A third-party viewer for [Second Life](https://secondlife.com) focused on
automation, content creation, accessibility and events.

**This software is not provided or supported by Linden Lab, the makers of
Second Life.**

6|7 World Studio is built on the [Alchemy Viewer](https://github.com/AlchemyViewer/Alchemy),
itself forked from the [Linden Lab viewer](https://github.com/secondlife/viewer),
and is licensed under the [GNU LGPL 2.1](LICENSE). See [NOTICE](NOTICE).

- Foundation decision, licences and policy: [doc/sw67/FOUNDATION.md](doc/sw67/FOUNDATION.md)
- Milestone 1 status, build and run instructions: [doc/sw67/MILESTONE-1.md](doc/sw67/MILESTONE-1.md)
- Milestone 2 (first local tools: workspace profiles, performance assistant, creator projects, product photography): [doc/sw67/MILESTONE-2.md](doc/sw67/MILESTONE-2.md)
- Roadmap, module map and plans: [doc/sw67/ROADMAP.md](doc/sw67/ROADMAP.md)
- RLV: [doc/sw67/RLV.md](doc/sw67/RLV.md)
- Upstream build reference: [doc/BUILD.md](doc/BUILD.md)

## Quick build

```
git clone --recurse-submodules https://github.com/jeffshanks-prog/six-seven-world-studio.git
cd six-seven-world-studio
cmake -S indra --preset vs2026-os        # Windows; Linux: ninja-os
cmake --build build-Windows-vs2026-os --config Release
```

Open-source presets (`-os`) only: they need no proprietary SDKs.

## Upstream

```
git remote add upstream https://github.com/AlchemyViewer/Alchemy.git
git fetch upstream && git merge upstream/main
```
