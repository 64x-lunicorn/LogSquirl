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
- Git's own messages (authentication failures, refused pushes) are what the user sees for failures. LogSquirl does not translate them.
- Two people changing the same group at the same time get a question instead of a lock. Nobody is ever locked out, and nobody's change is dropped without a decision.
- Tests run against real Git repositories with `file://` remotes, so CI needs Git, which its runners have.
