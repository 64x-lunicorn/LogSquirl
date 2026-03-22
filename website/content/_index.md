---
title: "About"
type: docs
---

## Faster log explorer

_logsquirl_ is an open source multi-platform GUI application to search through all kinds of text log files using regular expressions. It started as a fork of [klogg](https://github.com/variar/klogg) by [Anton Filimonov](https://github.com/variar), which itself was a fork of the [glogg project](https://glogg.bonnefon.org/) by [Nicolas Bonnefon](https://github.com/nickbnf). Since the original klogg is no longer actively maintained, _logsquirl_ continues development under a new name with many new features and improvements.

_logsquirl_ is designed to:

 - be very fast
 - handle huge log files
 - provide a clear view of the matches even in heavily cluttered files.

Here is what _logsquirl_ looks like:

<div id="gallery" style="display:none;">
<img src="/screenshots/mainwindow.png" alt="LogSquirl main window" />
<img src="/screenshots/dark.png" alt="LogSquirl dark theme" />
<img src="/screenshots/scratchpad.png" alt="LogSquirl scratchpad" />
<img src="/screenshots/highlighters.png" alt="LogSquirl highlighters configuration" />
</div>

## Features
{{< columns >}}
 _logsquirl_ inherited a lot of features from _glogg_ and _klogg_

 - Runs on Unix-like systems, Windows and Mac thanks to Qt
 - Displays search results separately from original file
 - Supports Perl-compatible regular expressions
 - Colorizes the log and search results
 - Displays a context view of where in the log the lines of interest are
 - Is fast and reads the file directly from disk, without loading it into memory
 - Watches for file changes on disk and reloads it (kind of like tail)

<--->

_logsquirl_ improves and brings more

 - Is optimized for modern CPUs with multiple cores and SIMD instructions
 - Has portable version (no need to install)
 - Understands a lot of text encodings and detects many of them automatically
 - Allows to perform search in a portion of huge log file
 - Combines regular expressions with boolean operators (AND, OR, NOT)
 - Supports multiple sets of text highlight rules with more sophisticated match options
 - Includes a Filters Panel with pinned filters that persist across sessions
 - JWT token decoder in Scratchpad for inspecting JSON Web Tokens
 - Import filters and highlighters from Chipmunk JSON export files
 - Opt-in beta update channel for early access to new versions
 - Provides many small features to make life easier (tab renaming, favorite files list, archive extraction,
 scratchpad etc.)
 

{{< /columns >}}

## Downloads

Latest beta version:

[ ![GitHub Release](https://img.shields.io/github/v/release/64x-lunicorn/LogSquirl?label=GitHub%20Release&style=for-the-badge)](https://github.com/64x-lunicorn/LogSquirl/releases/latest)


Latest development builds can be downloaded from releases on Github: 

{{< button href="https://github.com/64x-lunicorn/LogSquirl/releases/tag/continuous-win" >}}Windows{{< /button >}}
{{< button href="https://github.com/64x-lunicorn/LogSquirl/releases/tag/continuous-linux" >}}Linux{{< /button >}}
{{< button href="https://github.com/64x-lunicorn/LogSquirl/releases/tag/continuous-osx" >}}Mac{{< /button >}}

