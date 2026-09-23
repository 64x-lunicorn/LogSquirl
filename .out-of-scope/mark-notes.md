# Notes on Marks

A Mark is a Log Line the user has flagged, and nothing more. It carries no text:
there is no editor for a note on a Mark, no indicator in the bullet margin that a
Mark has one, and no list of notes to jump from.

## Why this is out of scope

The Scratchpad already covers it. It sits in the sidebar next to the Filters, takes
free text, and is where "line 48123: connection pool exhausted — this is where the
retries start" goes today. What a note bound to a Mark would add on top is small:

- a click in a list jumps to the line, instead of "Go to line..." with the number
  from the Scratchpad
- the bullet shows that a Mark has a note
- a truncation or reload clears the note together with the Mark

Against that stands a permanent cost in three places that are otherwise stable: a
new part of the Session's view state that old and new versions have to read, a
second kind of bullet in the margin painting, and a third sidebar tab. The Marks
themselves live in the Displayed Lines, which are tuned for speed over millions of
Log Lines; notes would have to be kept beside them, not in them, and kept in step
on every add, remove, truncation and reload.

The maintainer's call during triage was "ich denke nein erstmal" — not for now.
The need is real for long investigations with many Marks, but not common enough in
how LogSquirl is used to carry that cost. If that changes, delete this record and
reopen the request; the triage notes on #437 describe a design (a note map beside
the Marks, saved under its own key in the view state, a note only on a Mark,
tooltip on the bullet, a "Notes" sidebar tab).

## Prior requests

- #437: "A Mark carries a note"
