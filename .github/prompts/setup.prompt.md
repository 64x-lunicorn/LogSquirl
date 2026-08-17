---
description: 'Set this repo up for the Unisquirl skills — write .github/unisquirl.md, create CONTEXT.md, and start the review counter. Run once per repo, after installing the skills.'
---

Set this repo up so the other skills have what they assume. Three outputs:

| File | What it holds |
| --- | --- |
| `.github/unisquirl.md` | Which issue tracker this repo uses, and any triage label overrides. |
| `CONTEXT.md` | The project's vocabulary. Must exist — every skill writes in its terms. |
| `.github/.unisquirl-review` | The commit the last review ran against, so the session hook can tell when a review is overdue. |

Assume the user is **not** a developer. Ask in plain language, explain what a choice means before asking it, and never make them type a command or a path. Where you can find something out yourself, find it out — don't ask.

If `.github/skills/` doesn't exist, stop: the skills aren't installed. Tell the user to run `./install.sh <this-repo>` (or `./install.ps1 <this-repo>` on Windows) from the Unisquirl-Skills checkout first.

## 1. Look around first

- `git remote -v` — is there a GitHub remote? Which repo?
- `gh auth status` — is the `gh` CLI installed and logged in?
- `gh repo view --json hasIssuesEnabled` — are GitHub Issues actually switched on?
- `.scratch/` — is the local-markdown convention already in use?
- `.github/unisquirl.md` — has this run before? If so, show the current settings and ask what to change instead of starting over.
- `CONTEXT.md` / `CONTEXT-MAP.md` — does either exist already?
- `.github/skills/triage/` — is `triage` installed? If not, skip Section B entirely.
- The repo itself — README, package manifests, the top-level folders. You need enough to draft `CONTEXT.md` in step 3 without interrogating the user about their own project.

## 2. Where should issues live?

> Explain it like this: "When we plan work, the tasks have to be written down somewhere. Two options: as files inside this project, or as GitHub issues on the website."

Recommend **files in the project** unless all three are true: a GitHub remote exists, `gh` is logged in, and Issues are enabled. Files are the right answer for most people — nothing to sign up for, nothing to configure, and the tasks travel with the code.

- **Files in the project** (local markdown) — tasks live under `.scratch/<feature>/`. Operations: [`.github/reference/issue-tracker-local.md`](../reference/issue-tracker-local.md).
- **GitHub Issues** — tasks live on GitHub, driven by the `gh` CLI or the GitHub MCP server. Operations: [`.github/reference/issue-tracker-github.md`](../reference/issue-tracker-github.md).
- **Something else** (Jira, Linear, …) — only if the user brings it up. Ask them to describe how they use it, in their own words.

If GitHub looks available but one of the three checks failed, name the one that failed and recommend files anyway.

**Section B — triage labels.** Skip entirely if `triage` isn't installed. Otherwise ask one question: *"Should incoming bug reports use the standard labels?"* — recommended **yes**. On yes, record nothing; the defaults in [`triage-labels.md`](../skills/triage/triage-labels.md) apply. Collect overrides only if their tracker already uses different names.

## 3. Draft CONTEXT.md

`CONTEXT.md` is the project's glossary — the words this project uses and what each one means. It must exist before the other skills run, because every one of them writes in its vocabulary. An empty template is worse than none, so **draft it from what you read in step 1** and have the user correct you.

Propose:

```markdown
# Context

<One sentence: what this project is and who it is for.>

## Glossary

### <Term>

<What it means here, in one or two sentences. No implementation detail.>
```

Fill in the one-liner and two or three terms you already saw in the code or README, then ask: *"Have I got this right, and what would you add?"* Two or three correct terms beat ten guessed ones. `/domain-modeling` grows it from here every time a term gets sharpened.

Keep it a glossary. No implementation detail, no plans, no decisions — those are ADRs under `docs/adr/`, created later and only when a decision is hard to reverse.

If `CONTEXT-MAP.md` already exists, the repo has several contexts. Leave the layout alone and just fill any gaps.

## 4. Write

Show all three files, let the user correct them, then write:

**`.github/unisquirl.md`** — record the choice, not the how-to. The operations live in the reference files this points at; copying them here forks them. Omit any section that is just the default.

```markdown
# Unisquirl config

Written by `/setup`. Edit by hand any time.

## Issue tracker

**Files in the project** — see `.github/reference/issue-tracker-local.md` for the operations.
```

For a tracker you have no reference file for, write the user's description under `## Issue tracker`, covering create, read, list, comment, label and close plus the wayfinding operations — use the two reference files as the checklist. Without those, `/wayfinder` and `/to-tickets` have nothing to call.

**`CONTEXT.md`** — as agreed in step 3.

**`.github/.unisquirl-review`** — one line, the current commit (`git rev-parse HEAD`). This starts the counter the session hook reads; `/code-review` updates it on every run.

Never touch `AGENTS.md` or `.github/copilot-instructions.md`. Those load on every turn; this config is read on demand.

## 5. Done

Tell the user, in one short paragraph and without jargon, that the project is ready and what happens next. Mention they can just say `/ask` whenever they don't know which command to use.
