# Triage Labels

The skills speak in terms of five canonical triage roles. These are the default label strings.

| Canonical role    | Default label     | Meaning                                  |
| ----------------- | ----------------- | ---------------------------------------- |
| `needs-triage`    | `needs-triage`    | Maintainer needs to evaluate this issue  |
| `needs-info`      | `needs-info`      | Waiting on reporter for more information |
| `ready-for-agent` | `ready-for-agent` | Fully specified, ready for an AFK agent  |
| `ready-for-human` | `ready-for-human` | Requires human implementation            |
| `wontfix`         | `wontfix`         | Will not be actioned                     |

When a skill mentions a role ("apply the AFK-ready triage label"), use the matching label string.

**A repo can override these.** Check `.github/unisquirl.md` for a `## Triage labels` section first — a repo whose tracker already uses other names (`bug:triage` for `needs-triage`) maps them there, so `triage` applies the existing labels instead of creating duplicates. No section means the defaults above apply.
