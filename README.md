# LogSquirl

A fast, smart log file explorer.

> **This is a fork of the project [klogg](https://github.com/variar/klogg).**
> This fork has been renamed to "LogSquirl" and further developed.
> All changes are documented in the Git history.

[![PRs Welcome](https://img.shields.io/badge/PRs-welcome-brightgreen.svg?style=flat)](http://makeapullrequest.com)

## Overview

LogSquirl is a multi-platform GUI application that helps browse and search
through long and complex log files. It is designed with programmers and
system administrators in mind and can be seen as a graphical, interactive
combination of grep, less, and tail.

Please refer to the
[documentation](DOCUMENTATION.md)
page for how to use LogSquirl.

## Table of Contents

1. [About the Project](#about-the-project)
1. [Features](#features)
1. [Installation](#installation)
1. [Building](#building)
1. [How to Get Help](#how-to-get-help)
1. [Contributing](#contributing)
1. [License](#license)
1. [Acknowledgements](#acknowledgements)

## About the Project

LogSquirl is a fork of [klogg](https://github.com/variar/klogg), which itself started as a fork of
[glogg](https://github.com/nickbnf/glogg) - the fast, smart log explorer.

Since the original klogg project is no longer actively maintained, LogSquirl continues
development under a new name, building on the excellent foundation laid by both glogg and klogg.

## Features

* Runs on Linux, Windows, and macOS thanks to Qt
* Reads files directly from disk without loading them into memory
* Can operate on huge text files (10+ GB is not a problem)
* Search results are displayed separately from the original file
* Supports Perl-compatible regular expressions
* Colorizes the log and search results
* Displays a context view of where in the log the lines of interest are
* Watches for file changes on disk and reloads automatically (like tail)
* Is heavily optimized using multi-threading and SIMD
* Supports files with more than 2 billion lines
* Includes much faster regular expression search (2-4x)
* Allows combining regular expressions with boolean operators (AND, OR, NOT)
* Supports many common text encodings
* Detects file encoding automatically using [uchardet](https://www.freedesktop.org/wiki/Software/uchardet/)
* Can limit search operations to a portion of a huge file
* Allows configuring several highlighter sets and switching between them
* Has a list of configurable predefined regular expression patterns
* Includes a dark mode
* Has configurable shortcuts
* Has a scratchpad window for taking notes and doing basic data transformations
* Open source, released under the GPL-3.0

**[Back to top](#table-of-contents)**

## Installation

This project uses [Calendar Versioning](https://calver.org/).

### Building from source

Please review [BUILD.md](BUILD.md) for instructions on how to build LogSquirl
on your local machine.

**[Back to top](#table-of-contents)**

## Building

Please review
[BUILD.md](BUILD.md)
for how to set up LogSquirl on your local machine for development and testing purposes.

## How to Get Help

First, please refer to the
[documentation](DOCUMENTATION.md)
page.

You can open issues on the GitHub issues page.

## Contributing

We encourage public contributions! Please review [CONTRIBUTING.md](CONTRIBUTING.md) for details on our code of conduct and development process.

## License

This project is licensed under the GNU General Public License v3.0 or later - see [COPYING](COPYING) file for details.

## Acknowledgements

LogSquirl is built on the work of:

* **[klogg](https://github.com/variar/klogg)** by [Anton Filimonov](https://github.com/variar) and contributors (GPL-3.0)
* **[glogg](https://github.com/nickbnf/glogg)** by [Nicolas Bonnefon](https://github.com/nickbnf) (GPL-3.0)

See the [NOTICE](NOTICE) file for a full list of third-party components and their licenses.

**[Back to top](#table-of-contents)**
