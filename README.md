# OpenEdit

The plain text editor for OpenUp. It replaces TextEdit and Ed for everyday
editing: tabs, find and replace, syntax colours for scripts, Shell and ARexx
use, and safe saving. GadTools only, so it needs nothing beyond AmigaOS 3.2.

Runs on a 68040 with FPU under AmigaOS 3.2.3 with OpenUp, without OpenGPU.

Status: phase 1 written, waiting on its first m68k build. See [DESIGN.md](DESIGN.md).

## Building

With the os32 stove and OpenGadTools checked out beside this repo:

    ./build.sh                    # build/os3/OpenEdit and build/os3/Kinds/
    OGT=path/to/opengadtools ./build.sh

The text engine (gap buffer, undo, find, patterns, colour kinds) is plain C and
has host tests: `tests/host/run.sh`.

## Installing

- `C:OpenEdit`
- the kind files into `ENVARC:OpenEdit/Kinds/` (OpenEdit also looks in
  `PROGDIR:Kinds/`)
- `SetEnv SAVE EDITOR "C:OpenEdit WAIT"` so tools that call `$EDITOR` use it

## Using it from the Shell

    OpenEdit [FILES ...] [LINE n] [WAIT] [READONLY] [NEW] [PUBSCREEN name]

OpenEdit runs once. A second start hands its files to the running copy and
returns; with `WAIT` it returns when those tabs close.

## ARexx

Port `OPENEDIT`. With `OPTIONS RESULTS`, GET commands answer in `RESULT`.

| Command | Does |
|---|---|
| `OPEN file [LINE n]` | opens a file in a tab |
| `NEW` | a new Untitled tab |
| `FRONT` | brings the window to the front |
| `SAVE`, `SAVEAS file` | saves the current tab |
| `CLOSE [FORCE]` | closes the current tab |
| `TAB n` | goes to tab n |
| `GOTO line [col]` | moves the caret |
| `FIND text` | finds the next match (RC 5 when none) |
| `REPLACEALL text with` | replaces all, answers the count |
| `INSERT text` | types text at the caret |
| `GETTEXT`, `GETLINE [n]`, `GETFILE`, `GETPOS` | answers the text, a line, the path, or "line col" |
| `QUIT [FORCE]` | quits |

## Settings

`ENV:OpenEdit/Settings` (Use) and `ENVARC:OpenEdit/Settings` (Save): font, tab
width, spaces for tabs, auto-indent, line numbers, current line, colours, and a
colour per class for light and dark themes.

MIT licence, © 2026 Dalsin Limited.

## Contributors

OpenEdit is created and maintained by [SacredTrees](https://github.com/SacredTrees) with the AmigaChrome agent team, copyright Dalsin Limited. Everyone whose work it includes is credited in [`CONTRIBUTORS.md`](CONTRIBUTORS.md).
