# Skill mechanics

The skill-specific branch of [`writing-for-agents`](SKILL.md): what changes when the document is a skill, a prompt, or a custom agent — the primitive choice, frontmatter, the invocation choice, and router skills. Everything else about writing it is the universal reference in `SKILL.md`.

## Picking the primitive

VS Code offers three packages for a document the agent runs. They differ in what they carry and whose context they run in:

| Primitive | Path | Reach for it when |
| --- | --- | --- |
| **Skill** | `skills/<name>/SKILL.md` | A multi-step workflow, or one with bundled assets it points at — reference files, scripts, templates. Runs in the current context. |
| **Prompt** | `prompts/<name>.prompt.md` | One focused instruction with no assets. A skill whose body is a single paragraph is a prompt wearing a folder. |
| **Agent** | `agents/<name>.agent.md` | The work needs its own **context boundary** — a report comes back, the reading does not — or a **restricted toolset** (a reviewer that cannot edit, a scout that cannot write). |

Both skills and prompts appear under `/` in chat, so the user-facing difference is only the assets. The agent cut is the load-bearing one: an agent is the only primitive that clears context, which is what makes it the tool for parallel fan-out and for read-only roles.

An agent's frontmatter carries the restriction: `tools:` narrows what it can touch (`[read, search]` for research, add `edit` only where it genuinely writes), and `user-invocable: false` keeps a worker agent out of the picker when only other skills should reach it.

## Invocation

Two choices, trading the two loads:

- A **model-invoked** skill keeps a `description`, so the agent can fire it autonomously — and other skills can reach it. You can still type its name: model-invocation always _includes_ user reach; a description only ever adds agent discovery, never removes the human's. The description is the skill's top-level context pointer, forced to stay loaded at all times — permanent context load in exchange for discoverability. A model-invoked skill whose content is all reference is also one home for shared reference: another skill can invoke it, so reference needed by several skills lives in one place. Mechanics: omit `disable-model-invocation`, and write a model-facing description carrying the trigger branches (the pointer-writing rules in `SKILL.md` apply in full).
- A **user-invoked** skill strips the description from the agent's reach: only the human typing its name can invoke it, and no other skill can. Zero context load, but it spends cognitive load — you are the index that must remember it exists. Mechanics: set `disable-model-invocation: true`; the `description` becomes human-facing — a one-line summary, trigger lists stripped.

Pick model-invocation only when the agent must reach the skill on its own, or another skill must. If it only ever fires by hand, make it user-invoked and pay no context load.

Shared reference that two user-invoked skills both need can live in neither — with no descriptions, neither can fire the other. Push it to a plain file outside the skill system: external reference any skill can point at.

## Splitting by invocation

The invocation cut of splitting (the sequence cut lives in `SKILL.md`): split off a model-invoked skill when you have a distinct leading word that should trigger it on its own — a trigger word you actually use in your prompts — or another skill must reach it. You pay context load for the new always-loaded description, so that independent reach has to be worth it.

## Router skills

When user-invoked skills multiply past what you can remember, that piled-up cognitive load is cured by a **router skill**: one user-invoked skill that names the others and when to reach for each, so the human has one skill to remember instead of many. It can only hint, never fire them: user-invoked skills have no description, so nothing but the human can reach them.
