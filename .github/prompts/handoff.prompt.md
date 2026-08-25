---
description: 'Compact the current conversation into a portable handoff document another agent session can pick up.'
argument-hint: 'What will the next session be used for?'
---

Write a handoff document summarising the current conversation so a fresh agent can continue the work. Save it to the temporary directory of the user's OS — resolve it from `$TMPDIR`, falling back to `/tmp` (or `%TEMP%` on Windows) — not the current workspace. Tell the user the absolute path.

Include a **Suggested skills** section listing the skills and prompts the next agent should invoke.

Do not duplicate content already captured in other artifacts (specs, plans, ADRs, issues, commits, diffs). Reference them by path or URL instead.

Redact any sensitive information, such as API keys, passwords, or personally identifiable information.

If the user passed arguments, treat them as a description of what the next session will focus on and tailor the doc accordingly.
