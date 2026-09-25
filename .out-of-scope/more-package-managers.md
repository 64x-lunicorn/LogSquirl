# More Package Managers

LogSquirl is installed through the Homebrew cask on macOS and, once the package
repository is up (#379–#381), through apt and dnf on Ubuntu, Fedora and Oracle
Linux. Everyone else downloads a release: the NSIS installer, the DMG, the
AppImage, the DEB or the RPM.

It is not published through winget, Chocolatey, Scoop, the AUR, Flatpak or Snap,
and the repository does not carry templates for them.

## Why this is out of scope

Every package manager is a manifest that has to be updated on every release, or it
is worse than none: it keeps offering an old version, or a hash that no longer
matches, to exactly the users who trusted it to keep them current. The repository
had three such templates — an Arch `PKGBUILD` and a Scoop manifest with `TODO` for
their hashes, and a Gentoo ebuild pinned to 26.03.0 — that no workflow touched.
They were removed in #446.

The channels that are carried are the ones with a release job behind them and a
clear reach: Homebrew for macOS, apt and dnf for the distributions LogSquirl builds
packages for. For every other Linux, the AppImage runs without installing
anything. On Windows, the release download plus the update notice works.

The others, one by one:

- **winget** is the one worth revisiting. It would give Windows users
  `winget upgrade --all`, since LogSquirl does not update itself, and it is the
  shortcut companies use to deploy apps through Intune. It needs a token with a
  fork of `microsoft/winget-pkgs`, a first submission by hand and a release job
  that opens a pull request there on every stable release, and it goes smoother
  with a signed installer (#445). Not worth it until Windows users ask for
  updates or for company deployment.
- **Chocolatey** adds a moderation queue and a second Windows manifest next to
  winget, for a shrinking share of users.
- **Scoop** needs its own bucket or acceptance into a community bucket, for the
  portable build, for a small audience.
- **AUR, Flatpak, Snap** each need their own build recipe and review process;
  the AppImage already covers the distributions without apt or dnf.

Intune does not need anything from LogSquirl: an administrator wraps any
installer into an `.intunewin` package. The NSIS installer suits that: it
installs silently with `/S`, per machine, and records its version under the
machine's Uninstall key for detection — in the 32-bit view of the registry
(`WOW6432Node`), since the installer is a 32-bit program. The CI job *Windows
installer* checks this on every build, and the user guide's *Installing*
section tells administrators how to use it (#506).

## Prior requests

- #446: "Windows and Linux users install LogSquirl with their package manager"
