# Merging Log Files in Time Order

Log Merge puts its sources one after the other: every Log Line of the first
source, then every Log Line of the second, and so on, in the order of the tabs
the user merged ("Merge All Left" / "Merge All Right"). It does not interleave
the Log Lines of its sources by their timestamps, and it does not add a marker
saying which source a Log Line came from.

## Why this is out of scope

The maintainer's decision, in their words: "die logs sollen einfach nacheinander
aneinander gehängt werden! das möchte ich so" — the logs are to be appended one
after the other, and that is how it is meant to be.

What the concatenation gives, and a time-ordered merge would take away:

- The order of the merged Log File is the order the user chose, by arranging
  the tabs. Nothing about it depends on whether a timestamp could be parsed.
- Every Log Line stays exactly as it is in its source. No prefix, no column,
  no reordering of continuation lines such as stack traces.
- It works the same for every Log File, recognized Log Format or not, so there
  is no fallback case with different behaviour to explain.

A time-ordered merge would need a unit that turns a Log Line into a point in
time for every Log Format, rules for lines without a timestamp, per-source
Format Recognition and a way to show the source of each line. None of that is
wanted for Log Merge.

Deduplication (the "dedup" merge) and rebuilding the merged Log File when a
source changes are part of Log Merge and are not affected by this record.

## Prior requests

- #434: "Merging Log Files puts their Log Lines in time order"
