# Screen Reader Support

LogSquirl does not aim to be usable with a screen reader. It does not report
the Table View's selection to accessibility clients, it keeps no accessibility
interface of its own for its views, and it does not add accessible names to
dialogs so that a screen reader can announce them.

## Why this is out of scope

LogSquirl is for scanning log files by eye: millions of Log Lines, painted
colors from Highlighters, Marks, the Overview, charts. Reading log files
line by line through speech is not a way anyone works with it, and the
project is not built for that.

Supporting it anyway is not free, and every user pays for it. On macOS an
accessibility client is active far more often than a screen reader is: window
managers, launchers and similar tools switch it on. With it on, Qt's Cocoa
bridge answers each selection or focus event of a `QTableView` by building an
accessibility element for every Row again. For a Table View over 14 million
Log Lines, that made every click take 1.5 to 3.5 s instead of about 3 ms
(#425). The fix (000ca3ad, #426) was to stop the Table View from sending
those events.

Getting the selection announced again without that cost would mean an
accessibility interface of LogSquirl's own for the Table View (and, to be
consistent, for the Text View and the Filtered View), kept correct across
Qt upgrades and tested with real accessibility clients on three platforms.
That is a permanent cost in an area where it protects no feature
LogSquirl's users rely on, and one mistake there brings back the hang for
everyone with an accessibility client.

Accessible names that Qt sets up by itself, and fixes that keep
accessibility tools like window managers working, are not affected by this
decision. What is out of scope is work done *for* a screen reader.

## Prior requests

- #449: "The Table View tells a screen reader what is selected"
