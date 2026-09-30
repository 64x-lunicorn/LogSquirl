# The Archive Member Only in the Session

The window keeps its own table from a decompressed Log File's temporary path to the
archive member it came from, next to the same fact in the Session's entry for the
Open Log File. The request was to let the Session answer it and drop the window's
table.

## Why this is out of scope

The two copies do not live equally long, and that is on purpose. The Session's entry
dies with the tab. The window's table outlives it, so a Log File decompressed into
the window's temporary folder before is known by its archive again: the recent
files, the tab names and tab groups (#609), and the folder proposed for *Save
Session As...* all use the archive, not the temporary path. The table is emptied
only when an open fails (#643), and the whole folder goes with the window.

Moving the fact into the Session would mean keeping Session entries after their tab
closes, or losing the "known again" case. Either costs more than the table it
removes. Since #643 the open call already carries the archive member into the
Session, so the two copies are filled from one value and cannot drift apart when a
tab opens.

If the Session one day keeps closed Log Files anyway (for example to reopen a closed
tab), delete this record and reopen the request.

## Prior requests

- #637: "The Session says which archive member a view came from; the window keeps no copy"
