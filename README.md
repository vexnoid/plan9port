# plan9port, acme fork

A fork of [plan9port](https://github.com/9fans/plan9port), a port of
many Plan 9 libraries and programs to Unix. Most changes are in acme
and the programs around it.

## Installation

Run `./INSTALL`; see `install.txt` or install(1) for details. After a
successful install, `9 man 1 intro` lists the man pages for new
features and differences from Plan 9.

acme now needs a mouse with side buttons.

## What differs from upstream

**Markup.** Each acme window has a `markup` file through which a
client sets fonts, colors, underlines and strikethroughs on ranges
of the body, for syntax highlighting, colored diffs and the like. Fonts may be any height.
The markup is kept on the file, so it follows the text through edits
and undo. See acme(4) and frame(3).

**Input.** Undo and redo are on the side mouse buttons. Scrolling is
only on the scroll bar, and the wheel over a layout box expands or
shrinks the tag. Buttons 2 and 3 chord to pass the snarf buffer as an
argument. Reverse search, the arrow and page keys, and button
remapping with Ctrl and Alt are gone.

**Tags.** Tags are plain text with their own undo, and acme no longer
rewrites their commands. Selections in them survive Get, Dump and
Load. Windows can be marked scratch.

**Dump.** Dumps now keep scroll position, tag selections and event
windows with their content and zeroxes. The default file is `adump`.

**Layout.** A window box replaces window decorations in devdraw, and
button 3 in the layout box toggles a full-size window without losing
the old geometry. Resizing on X11 no longer flashes or leaks.

**Events.** Event text is never cut off at 256 characters, and a
chorded argument can be written back to acme.

**Commands.** Added Pack, and Get on files. Snarf is now Copy.
Removed Local, ID, Abort, Send (win does it), URL and include
handling, acme `-b`, `-D` and `-r`, and mail.

There are also many smaller fixes to Edit addresses, win, sed, ls
and libframe; see `git log`.

## Incompatible changes

- Old dumps cannot be loaded.
- Scripts that use the removed commands or `Snarf` no longer work.
- A window's ctl and index files have a sixth number, the scratch flag.
- Put is no longer hidden when a program opens the event file.
- `ls -F` marks only executables; use `-D` for directories.
