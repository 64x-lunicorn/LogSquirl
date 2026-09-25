---
title: Automatic Crash Reporting
description: How LogSquirl uses Sentry and Breakpad/Crashpad to collect crash dumps while respecting user privacy.
---

In response to several GitHub issues about unexpected crashes, LogSquirl implemented crash dump collection. Thanks to the [SDK](https://github.com/getsentry/sentry-native) provided by [Sentry](https://sentry.io), it is fairly easy to integrate Breakpad/Crashpad to collect minidumps for application crashes and send them to developers for investigation.

## What is included in crash report

Crash report provides information about:

- Operating system: name, version, architecture
- Qt version
- Modules that were loaded into the LogSquirl process: file path, size and hashes for symbols
- Stacktraces for all running threads in the LogSquirl process, with their stack memory

The minidumps do not include the whole memory of the LogSquirl process, but the stack memory of each thread, which can hold fragments of what LogSquirl was working on at the moment of the crash.

## A word about privacy

Although crash dumps can make fixing bugs easier, privacy of LogSquirl users is far more important. So crash reporting is not automated. If during startup LogSquirl finds minidumps from previous runs, it will show a dialog asking the user to look through the generated crash report and confirm sending it to Sentry servers. Unsent reports are deleted. Crash reports carry no hardware ids or host names and no account of any kind, but a module's file path can contain the user name, and Sentry sees the IP address a report comes from.

Please check the [Privacy Policy](/privacy-policy) for more details.
