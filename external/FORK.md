# ps2-web-recomp fork

- Upstream: https://github.com/snuri00/ps2-web-recomp (MIT, (c) 2026 Samir Nuri)
- Our fork: https://github.com/BlissVibes/ps2-web-recomp, branch `burnout3`
- Pinned commit: `17a5eee43eb013d43f26ace6dbff5139e5e8043c` (upstream `17a5eee43eb013d43f26ace6dbff5139e5e8043c`, 2026-10-10)
- Local clone (WSL): `~/b3/ps2-web-recomp`

Why not a submodule: the fork's `port.sh` writes `games/`, `generated/` and `build/`
inside its own tree. Those hold game-derived files that must never enter this repo,
so the fork lives outside the repo and is pinned here instead.

## Update procedure
1. `cd ~/b3/ps2-web-recomp && git fetch upstream && git rebase upstream/main`
2. Rebuild: `scripts/setup.sh`, then rerun `tools/port.sh` from this repo.
3. If the headless run still passes `tools/scan-log.py`, `git push --force-with-lease`,
   update the pinned commit above, and note the upstream date.

## Our changes on `burnout3` (keep generic, offer upstream)
- `cmake: PS2_WEB_SHELL lets a port supply its own shell page`: `CMakeLists.txt` gains a
  `PS2_WEB_SHELL` cache variable (default: the fork's own `web/shell.html`) used for
  `--shell-file` and the link dependency. `tools/build-web.sh` points it at this repo's shell.
- `analysis: seed functions from pointer tables in data`: `recomp/analysis.cpp` gains
  `pointer_tables()`; eerecomp now translates functions reached only through a table of four
  or more code pointers (static constructor lists, vtables). See
  `docs/re/gap-001-static-constructor-table.md`.
