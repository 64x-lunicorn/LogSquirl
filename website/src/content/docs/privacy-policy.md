---
title: Privacy Policy
description: What LogSquirl sends over the network — the update check, crash reports only with your consent, and what happens only when you ask — and how to turn it off.
---

<!-- The same policy as PRIVACY.md in the repository, which the Windows installer shows; change both together (#445). -->

LogSquirl reads your log files on your own computer. It has no accounts, no analytics and no usage statistics, and it never sends the content of your log files anywhere.

On its own, without you asking, LogSquirl contacts exactly one server: the **update check**. It is on by default, and this policy says how to turn it off. Everything else that uses the network happens only when you ask for it.

This policy covers LogSquirl as built and released by the project at [github.com/64x-lunicorn/LogSquirl](https://github.com/64x-lunicorn/LogSquirl). It is maintained by Daniel Löffler (GitHub: [64x-lunicorn](https://github.com/64x-lunicorn)). Contact: info@lunicorn.de. See also the [legal notice](/legal-notice/).

## 1. The update check (automatic, on by default)

**What is sent:** LogSquirl downloads the file [`latest.json`](https://raw.githubusercontent.com/64x-lunicorn/LogSquirl/master/latest.json) from the LogSquirl repository with an ordinary HTTPS request. The request carries nothing about you or your installation: no LogSquirl version, no identifier, no settings, no file names. LogSquirl compares the versions listed in that file with its own version on your computer, and shows a notice when a newer one is out.

**When:** when LogSquirl starts, at most once every seven days. The date of the next check is kept in LogSquirl's settings on your computer. If you turn on *Check for beta updates*, LogSquirl checks at every start.

**Who receives it:** GitHub, which serves the file. Like any web server it sees your IP address and the standard headers of the request. See [GitHub's privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).

**How to turn it off**, any one of these:

- In LogSquirl: *Options → General* → untick *Check for new version*.
- In the Windows installer: untick the component *Check for updates automatically*. The check is then off for that installation, whatever the settings say; the Options show the box greyed out. Run the installer again with the component ticked to turn it back on.
- For any installation: put an empty file named `logsquirl_no_update_check` next to the LogSquirl executable. This is what the Windows installer does.

## 2. Crash reports (only if you agree, every time)

The official builds contain a crash handler (Crashpad, through the Sentry Native SDK). If LogSquirl crashes, a crash report is written to your computer, in the folder `logsquirl_dump` in LogSquirl's application data folder (the portable Windows build: next to the executable).

**Nothing is sent when the crash happens.** The next time LogSquirl starts, it shows you the report and asks. Only if you click *Send report* is it uploaded; *Discard report* deletes it. There is no setting that sends reports without asking.

**What a report contains:**

- A minidump: the state of the crashed LogSquirl process, that is the stack of every thread with its stack memory and processor registers, and the list of loaded modules (their file paths and versions). Stack memory can hold fragments of what LogSquirl was working on at that moment, for example part of a log line, and a file path can contain your user name.
- The operating system's name and version and the processor type.
- The LogSquirl version and the commit it was built from, the Qt version and the build architecture.
- The size of the computer's memory, the processor instruction sets LogSquirl can use, the number of worker threads, and how much memory and processor time LogSquirl used.

**Who receives it:** [Sentry](https://sentry.io) (Functional Software, Inc.), in its EU data region (the reports go to `ingest.de.sentry.io`). Sentry also sees the IP address the report comes from. The LogSquirl maintainers use the reports only to find and fix crashes. See [Sentry's privacy policy](https://sentry.io/privacy/).

After a crash LogSquirl also offers to open a new GitHub issue in your browser. The issue text is filled in with the LogSquirl version, build date and commit, the operating system and its version, the processor architecture, the number of worker threads, the Qt and TBB versions and the crash id. Nothing is sent unless you submit that form on GitHub yourself.

## 3. Only when you ask

- **Plugins:** opening the Plugins dialog downloads the [plugin catalog](https://raw.githubusercontent.com/64x-lunicorn/LogSquirl-Plugins/main/plugins.json), and then each listed plugin's release list and icon from the addresses the catalog gives (today all on `raw.githubusercontent.com`). Installing a plugin downloads it from the address its release list gives (today `github.com`). These requests identify themselves as `LogSquirl-PluginRepo/2.0` and carry nothing else about you. A plugin is a separate program by its own author; what an installed plugin does is not covered by this policy.
- **Open from URL:** downloads the address you enter, from that server.
- **Team Folder** (off by default): when you turn it on and enter a Git repository, LogSquirl runs the Git program installed on your computer to clone, update and push to that repository. The filter groups you publish there are committed under the name and e-mail address your Git is configured with.
- **Links** you click, such as those to the documentation or to GitHub, open in your web browser.

## 4. On your computer

LogSquirl keeps its settings, the list of recent files, the session and, if you turn them on, its index cache and its own log file on your computer. They are not sent anywhere except as described above. The Windows uninstaller removes the settings file.

## 5. This website

This website sets no cookies and loads no analytics or third-party scripts. Like any web server, the web host sees your IP address when it delivers a page. Downloads are served by GitHub.

## Changes

Changes to this policy are made in [`PRIVACY.md`](https://github.com/64x-lunicorn/LogSquirl/blob/master/PRIVACY.md) in the LogSquirl repository, whose [history](https://github.com/64x-lunicorn/LogSquirl/commits/master/PRIVACY.md) shows every change.
