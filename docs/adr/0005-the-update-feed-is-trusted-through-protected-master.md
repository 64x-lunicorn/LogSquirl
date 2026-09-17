# The Update Feed is trusted through protected master, and the app checks only where an Update Offer points

LogSquirl learns about new releases from the Update Feed, `latest.json` on master, which it downloads from the repository. Until #222, the release workflow wrote the recorded release straight to master with `[skip ci]`, past the pull request and status checks that protect every other change, and nothing in the application could tell a genuine announcement from a forged one. Two things had to be decided: where the Update Feed lives and how it is written, and whether the application verifies an announcement before it offers it.

The Update Feed stays `latest.json` on master. The release workflow commits the recorded release to the branch `feed/<tag>`, and it reaches master through a pull request with the normal checks, which validate the feed (`release-feed.py check`). No direct push and no ruleset bypass. The integrity of the Update Feed's content is what protected master already guarantees for the code: nobody changes it without a reviewed, checked pull request.

The application verifies no signature and no checksum. It offers a release only when its `stable_url` or `beta_url` begins with exactly `https://github.com/64x-lunicorn/LogSquirl/releases/`; any other release is not offered, and the rejection is logged. The Update Offer only shows a link to a Release Page, where GitHub serves the release, its attestations and its signed checksum file, so the one thing a tampered Update Feed could still do through the app is point the user elsewhere, and that is what the check prevents. The URL is compared both as text and as parsed by `QUrl` (scheme, host, no user info or port, the path below the releases without `.` or `..` segments or backslashes), so neither a lookalike prefix such as `…/releases.evil.com` nor a path that climbs out of the releases passes.

## Considered Options

- **The release bot keeps pushing to master, with an explicit ruleset bypass.** Rejected: master would have one writer that skips the checks, and a compromised release job could announce anything.
- **The Update Feed moves out of the repository**, to a release asset or the website. Rejected: every running LogSquirl already reads `latest.json` from master, and neither place is better protected than master's ruleset.
- **The application verifies a signature or the signed checksum file** before offering a release. Rejected: it needs a key or verifier shipped in the app and rotated with it, while the app downloads no release itself; the user downloads from the Release Page, where the checksum file and attestations already are.

## Consequences

- A release is announced in the app only once someone opens and merges the `feed/<tag>` pull request.
- A release whose URL the check rejects is silently skipped for the user; if the stable release is rejected, a newer beta can still be offered to a user who checks for betas.
- Moving the repository, or the releases to another host, needs a new app version before those releases are offered. Builds already installed keep offering nothing newer than the move.
- The check guards where the Update Offer points, not what version it names: a forged version number that reached master would still be announced, with a link to a real Release Page.
