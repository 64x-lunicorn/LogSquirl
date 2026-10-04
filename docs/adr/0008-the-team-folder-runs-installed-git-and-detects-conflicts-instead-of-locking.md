# The Team Folder runs the installed git, and detects conflicts instead of locking

A team wants to share Filter Groups and Highlighter Sets: one person changes a group, and everyone else has the change without doing anything. The team already has a Git server and Git on every machine, so the shared place is a Git repository that LogSquirl clones and writes to — the Team Folder. Two things had to be decided: how LogSquirl talks to Git, and what stops two people from overwriting each other's change to the same group.

LogSquirl runs the installed `git` program as a separate process, from one module that owns the Team Folder and is the only part of the application that runs Git. It links no Git library. The user's existing authentication — SSH keys and agent, the platform credential manager, proxy and certificate settings in their Git configuration — applies unchanged, and LogSquirl never sees or stores a credential.

Nothing is locked. Every group is its own file in the Team Folder. When a Team group is loaded for editing, LogSquirl remembers the revision of its file. Publishing syncs first. If that file changed since the remembered revision, the user decides: keep mine, take theirs, or save mine as a copy. A push rejected because someone pushed in between is retried once after syncing. Changes to different groups never meet. Who may change Team groups is whoever the Git server lets push; LogSquirl has no roles of its own.

## Considered Options

- **A linked Git library (libgit2).** Rejected: LogSquirl would have to implement authentication itself, for SSH, HTTPS, credential managers and proxies on three platforms, which is where tools like this usually fail. It would also add a dependency to every package, for something every team member already has installed.
- **Locks with an expiry**, a lock file committed and pushed before editing. Rejected: Git has no atomic lock. Two people can take the lock at the same time, and the loser only finds out when pushing — the same moment conflict detection already catches. A lock left by a crashed or closed LogSquirl blocks the group until it expires. An expiry short enough not to hurt is too short for a long edit.
- **A non-binding "being edited by" hint.** Deferred: it still has to be pushed, so it arrives late and stays behind when LogSquirl closes without cleaning up. It only adds to conflict detection, which is enough on its own.
- **Pull requests for every change.** Rejected: the point is that a change reaches everyone at once. A team that wants review can protect its branch; LogSquirl then reports the refused push.

## Consequences

- LogSquirl starts an external program that can reach the network. That is new for the application, and it is confined to the Team Folder module.
- Git must be installed for the Team Folder. Without it the feature reports that and stays off; nothing else depends on it.
- Git's own messages (authentication failures, refused pushes) are what the user sees for failures. LogSquirl does not translate them. There is one exception: it reads, but does not translate, a single message. A push that fails with Git's fixed sentence `The requested URL returned error: 403` on standard error (Git runs with `LC_ALL=C`) counts as refused, like a `[remote rejected]` ref, and the Team groups turn read-only. The sentence is matched, not a bare number, so a URL that contains 403 never matches. Every other failure of a push that is not a rejected ref stays "unreachable", so a network error never discards or blocks a local commit. Beside it, LogSquirl recognises a few more fixed sentences, only to add a hint (see the amendment below).
- Two people changing the same group at the same time get a question instead of a lock. Nobody is ever locked out, and nobody's change is dropped without a decision.
- Tests run against real Git repositories with `file://` remotes, so CI needs Git, which its runners have.

## Amendment: hints for common failures (#713)

Git's raw text alone left users stuck on common, well-understood failures, such as a clone refused because the organization enforces SAML SSO. LogSquirl still never rewrites or translates Git's messages, and the details show Git's output unchanged. In addition, it may recognise a listed set of Git's fixed English sentences (Git runs with `LC_ALL=C`) to add a short, translated hint under the status heading that says what to do:

- **Sign-in failed**: an SSH `Permission denied (<methods>).`, `fatal: Authentication failed for '<url>'`, or `fatal: could not read Username|Password for '<url>': ...`.
- **Organization requires SSO authorization**: `The <org> organization has enabled or enforced SAML SSO.`, as GitHub prints it, with or without `ERROR: ` or `remote: ` before it.
- **Repository not found**: `ERROR: Repository not found.` or `remote: Repository not found.`, `fatal: repository '<url>' not found`, `fatal: '<path>' does not appear to be a git repository`.
- **Server unreachable**: `ssh: Could not resolve hostname ...`, `ssh: connect to host <host> port <n>: ...`, and `fatal: unable to access '<url>': ` followed by a resolve, connect or time-out failure.
- **Git is not installed**: Git could not be started at all.

A sentence matches only as a whole line of Git's output, with the URL, path or host where Git puts it, never as a bare number or a word inside a line, so a URL or path that holds the words of a sentence matches nothing. A failure that is not recognised shows the heading and the details only. The 403 rule above stays as it is: a refused push gets no hint of its own.
