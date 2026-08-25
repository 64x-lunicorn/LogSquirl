# Domain Docs

How the skills should **consume** this repo's domain documentation when exploring the codebase. (Changing the model — writing the glossary, recording ADRs — is the `/domain-modeling` skill, which also owns the file layout.)

## Before exploring, read these

- **`CONTEXT.md`** at the repo root, or
- **`CONTEXT-MAP.md`** at the repo root if it exists — it points at one `CONTEXT.md` per context. Read each one relevant to the topic.
- **`docs/adr/`** — read ADRs that touch the area you're about to work in. In multi-context repos, also check `src/<context>/docs/adr/` for context-scoped decisions.

`CONTEXT.md` always exists in a set-up repo — `/setup` creates it. If it is missing, the repo was never set up: say so and point at `/setup` rather than carrying on with invented vocabulary.

ADRs are the opposite: they appear only as decisions get made. An empty or absent `docs/adr/` is normal — proceed silently, and let `/domain-modeling` create one when a decision actually earns it.

## Use the glossary's vocabulary

When your output names a domain concept (in an issue title, a refactor proposal, a hypothesis, a test name), use the term as defined in `CONTEXT.md`. Don't drift to synonyms the glossary explicitly avoids.

If the concept you need isn't in the glossary yet, that's a signal — either you're inventing language the project doesn't use (reconsider) or there's a real gap (note it for `/domain-modeling`).

## Flag ADR conflicts

If your output contradicts an existing ADR, surface it explicitly rather than silently overriding:

> _Contradicts ADR-0007 (event-sourced orders) — but worth reopening because…_
