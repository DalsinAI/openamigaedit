# Restart: OpenEdit

_Written 6 October 2026 at about 23:55 UTC, while all work is paused on @SacredTrees's word (23:28 UTC). Read this first when work resumes; the newest capsule and the live PR list win if they disagree._

## What this repo is

OpenEdit, the plain text editor for OpenUp in place of TextEdit and Ed: tabs, find and replace, syntax colours, Shell and ARexx use, safe saving. GadTools only.

## Where it stands

Phase 1 is merged (#1) and ships in OpenUp 0.6.13; Main Discourse built it on the home PC (104,336 bytes, no warnings). The repo's default branch is still claude/project-thread-1bw5tr; main also exists at 672f0ef. This restart note is on both.

## Merged lately

- #1 (672f0ef, 2026-10-06): OpenEdit phase 1: tabs, find and replace, colours, Shell and ARexx, safe save
- #2 (c2df873, 2026-10-06): Credit who made OpenEdit: CONTRIBUTORS.md

## Open pull requests

- None.

## Next step

1. Route text files to OpenEdit in OpenTypes and set ENV:EDITOR to "C:OpenEdit WAIT" (Main Discourse packaging).
2. Make main the default branch.
3. Phase 2: split view, bookmarks, compare, macros.

## Waiting on @SacredTrees

- The go for phase 2.
- OK to switch the default branch to main.

## Who owns it

OpenEdit thread; Main Discourse for packaging.

## Capsules

Restart capsules for this repo's workstreams, in amigachrome's `capjumps/` shelf:

- [`20261006_AmigaChrome_OpenEdit_Restart_Capsule.zip`](https://github.com/DalsinAI/amigachrome/tree/main/capjumps)

Team rules that still hold: commits as SacredTrees with no co-author lines; third-party code only on "yes with review" (licence checked, commit and sha256 pinned, fetched at build, never committed); deploys with deploy_dev.py only, on a typed line.
