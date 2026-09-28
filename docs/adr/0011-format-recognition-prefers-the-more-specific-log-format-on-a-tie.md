# Format Recognition prefers the more specific Log Format on a tie

Format Recognition picks the Log Format that matches the most of a Log File's first Log Lines. Several built-in Log Formats match the same Log Lines, though, and until #589 a tie went to the Log Format with the most capture groups in any one of its patterns, whether or not that pattern matched. spdlog's lines (`[2026-09-09 09:41:00.100] [gateway] [info] GET /api/catalog …`) are also Apache error log lines to `error_log`'s `apache` pattern, which takes `[gateway]` as the level and the rest as the body. That pattern has nine capture groups, most of them optional and empty on such a line; spdlog's widest pattern has seven, so the spdlog Log File opened as an Apache error log. The same rule read a CUPS log as `error_log`, whose widest pattern is the `apache` one again.

It was not even stable: both the Log Format Catalog and each Log Format's patterns are held in a `QHash`, whose order changes from run to run, and a tie between equal counts was decided by that order. `java_log`'s own sample lines came out as `java_log` in one run and `syslog_log` in the next.

## Decision

Among the Log Formats that match the most sample Log Lines, the tie is decided by, in this order:

1. **Which one is more specific.** A Log Format is more specific than a rival when the rival's patterns accept one of its sample lines (the `"sample"` section every lnav format carries), and its own patterns accept none of the rival's. `error_log` accepts spdlog's sample `[2014-10-31 23:46:59.678] [my_loggername] [info] Some message`; `spdlog_log` accepts none of Apache's, so spdlog is the more specific. The candidate with the fewest more specific rivals wins. It uses knowledge the Log Formats already carry instead of guessing from the shape of a regex.
2. **Which one captures more fields of the Log Lines.** Counted over the matching Log Lines, with the pattern that captures the most for each: a named group counts only when it took part in the match, so optional groups that stayed empty count for nothing, and a pattern that does not match counts not at all. For a JSON or logfmt Log Format, the declared fields a Log Line holds.
3. **The name.** The Log Format first by name, so the answer never depends on the order of the Catalog.

The match count and the threshold (at least half of the sample Log Lines) do not change, and neither does the precedence of regex and JSON Log Formats over logfmt ones.

## Considered Options

- **Only counting captured fields.** Fixes spdlog (four fields against Apache's three), but a loose pattern that reads every word into a field of its own wins: `java_log` reads `PDT` as the level and `LOG` as the class of a PostgreSQL line and so captures one field more than `postgres_log` on the default PostgreSQL prefix (`2025-08-13 01:45:01.127 PDT [12347] LOG: …`); HAProxy's syslog-wrapped lines would go to `syslog_log` the same way.
- **A tighter `apache` pattern.** Would fix this one pair and leave the rule that caused it in place; the built-in formats come from lnav and are meant to stay close to it.
- **Checking that a level field holds a level.** Most built-in Log Formats declare no level mapping, and the levels in use (`*` and `#` in Redis, `LOG` and `STATEMENT` in PostgreSQL) would need a vocabulary of our own.

## Consequences

- Every built-in Log Format's sample lines are now recognized as that Log Format, one by one and together, in every run; a test pins this. The one exception is the CUPS sample line `error_log` carries along with a copy of the CUPS pattern: `cups_log` reads it, which is what it is.
- A Log Format without sample lines, as a user's own often is, can never be the more specific one: it loses to every tied rival whose sample lines its patterns accept, and otherwise rule 2 decides. Giving it sample lines lets it win over a looser Log Format that also matches.
- The rule is only evaluated among the tied candidates, and only their sample lines are matched, so it costs a few regex matches per recognition.
