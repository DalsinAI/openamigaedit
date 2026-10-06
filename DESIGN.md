# OpenEdit design, v0.1

Team, 6 October 2026. Design only; nothing is built until the screens are approved.
Mockup: https://claude.ai/artifact/CERDfmL4ayxsywp6WUBpGY

## What it is

A plain text editor for OpenUp that replaces TextEdit and Ed for everyday editing:
S:Startup-Sequence, ARexx scripts, readmes and source code. It is not a word
processor (that is OpenWrite, a separate project).

Target: 68040 with FPU, AmigaOS 3.2.3, OpenUp, no OpenGPU. Works from 640x256 AGA
in 16 colours with Topaz 8 up to 1920x1080 OpenRTG.

## Window

- One window, a tab per file (the OpenFiles model). A dot on a tab marks unsaved
  changes. Opening a file that is already open goes to its tab. A tab can be
  dragged out into its own window.
- Menus: Project, Edit, Search, View, Tools, Settings.
- Line number gutter and current-line highlight (both switchable).
- Find and replace bar under the text, not a separate window. Match case, whole
  words, AmigaDOS patterns (`#?`), All tabs. Matches are marked in the text and in
  the scroller.
- Status line: Line/Col, line count, colour kind, character set and line ends,
  Insert/Overwrite, saved state. Clicking a part changes it.

## Text engine

- Our own text area on OpenGadTools. No MUI, no TextEditor.mcc.
- Buffer: a gap buffer per file plus a line-start index; undo as a list of
  insert/delete records grouped by typing pause. Files to several MB stay quick
  on a 68040.
- Drawing: `Text()` per line, only changed lines redrawn; scrolling with
  `ScrollRaster()`. Colouring is computed per line with a carried state (inside a
  comment or string), so an edit recolours from the changed line until the state
  settles.
- The text area is written so it can later become an OpenGadTools gadget, for the
  OpenFiles preview and other apps.

## Colours

- The kind comes from OpenTypes (the same lookup OpenFiles uses), with the file's
  extension as a fallback.
- Kinds are small text files in `ENVARC:OpenEdit/Kinds/` (keywords, comment marks,
  string marks, number and variable rules). First set: AmigaDOS script, ARexx, C,
  Assembler (Devpac), AmigaGuide, HTML, Lua, Makefile. Adding a kind needs no
  rebuild.
- Seven pens: commands, keywords, strings, variables, numbers, labels, comments.
  Pens are obtained with `ObtainBestPen()`; on a screen with too few free pens
  they fold onto bold and plain text.
- Colour schemes: Classic first; Open 4 follows the OpenLook "Open 4" theme.

## Characters and line ends

ISO-8859-1 by default; UTF-8 when the file has a BOM or decodes cleanly as UTF-8
with non-ASCII bytes. Line ends (LF, CR LF, CR) and the character set are kept as
found. UTF-8 characters the font lacks show as a box and are saved back unchanged.

## Saving

Write a temporary file beside the target, then rename it over the old one, so a
crash or a full disk never leaves a half-written file. Files in S: and DEVS: keep
the previous version as `.bak`. Protection bits, comment and date are carried over.
Closing a window with changes asks once for all tabs.

## Shell and ARexx

Template: `FILES/M,LINE/N,WAIT/S,READONLY/S,NEW/S,PUBSCREEN/K`.

- Single copy: a second start hands its files to the running copy through the
  ARexx port and exits (or, with WAIT, waits for those tabs to close).
- `WAIT` lets scripts and tools that call `$EDITOR` keep working.
- ARexx port `OPENEDIT`: OPEN, SAVE, SAVEAS, CLOSE, GOTO, FIND, REPLACE, INSERT,
  GETTEXT, GETLINE, QUIT.

## Settings

Colour scheme, font (fixed-width by default), tab width and insert-spaces, line
numbers, current line, kind colours. Save writes ENVARC:, Use writes ENV:, as in
every OpenPrefs editor.

## Fitting into OpenUp

- Installed as `C:OpenEdit` (with a Tools icon). OpenUp sets
  `ENV:EDITOR` to `C:OpenEdit WAIT` and points OpenTypes text kinds at OpenEdit.
- C:Ed and SYS:Tools/TextEdit stay installed as the fallback.
- Packaging into OpenUp goes through the OpenUp release owner.

## Phases

1. Tabs, undo/redo, cut/copy/paste (clipboard.device), find and replace, colours,
   Shell and ARexx use, safe save, settings.
2. Split view, bookmarks, compare two files, recorded macros.

## Decisions (recommended options first)

1. Replace by default, keep the old ones (ENV:EDITOR + OpenTypes; C:Ed and
   TextEdit stay). Recommended.
2. Our own text area on OpenGadTools, no MUI. Recommended.
3. Tabs in one window, one copy running. Recommended.
4. First eight colour kinds as listed. Recommended.
5. ISO-8859-1 default, UTF-8 when the file says so; keep line ends. Recommended.
6. Phase 1 scope as above. Recommended.
