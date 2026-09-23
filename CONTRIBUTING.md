# Introduction

Thank you for considering contributing to LogSquirl.
It's people like you that make LogSquirl such a great tool.

Following these guidelines helps to communicate that you respect 
the time of the developers managing and developing this open source project.
In return, they should reciprocate that respect in addressing your issue,
assessing changes, and helping you finalize your pull requests.

There are many ways to contribute, from writing tutorials or blog posts,
improving the documentation, submitting bug reports and feature requests
to writing code which can be incorporated into LogSquirl itself.

# Ground Rules

 * Keep pull requests and issues as small as possible, preferably one new feature or bug description per request.
 * Ensure cross-platform compatibility for every code change: Windows, Mac, Ubuntu Linux.
 * Create issues for any major changes and enhancements that you wish to make. Discuss things transparently and get community feedback.
 * Be welcoming to newcomers and encourage diverse new contributors from all backgrounds.
  See the [Code of Conduct](CODE_OF_CONDUCT.md).

# How to suggest a feature or enhancement

LogSquirl is intended to be a log *viewing* tool with additional features that help
navigate through text files, extract information and reconstruct chain of events.

If you find yourself wishing for a feature that doesn't exist in LogSquirl,
you are probably not alone. There are bound to be others out there with similar needs.
Many of the features that LogSquirl has today have been added because our users saw the need.
Open an issue on GitHub with the **Feature request** form, which asks what you are trying
to find out in your logs, how you answer that question today, and where LogSquirl stops
being able to help. Start there rather than with the feature you had in mind: the question
tells us whether LogSquirl can already answer it, and what the best answer would be.
Would you rather talk an idea through first? Use the
[Ideas discussions](https://github.com/64x-lunicorn/LogSquirl/discussions/categories/ideas).

# How to report a bug

If you find a security vulnerability, do **NOT** open an issue. 

In order to determine whether you are dealing with a security issue, ask yourself these two questions:
 * Can I access something that's not mine, or something I shouldn't have access to?
 * Can I disable something for other people?

 If the answer to either of those two questions are "yes", then you're probably dealing with a security issue.
 Note that even if you answer "no" to both questions, you may still be dealing with a security issue,
 so if you're unsure, open a
 [security advisory](https://github.com/64x-lunicorn/LogSquirl/security/advisories/new)
 rather than an issue, as [SECURITY.md](SECURITY.md) asks.

The **Bug report** form asks these five questions, so filling it in answers them all:

1. What version of LogSquirl are you using (version is listed in window title and about dialog)?
1. What operating system are you using?
1. What did you do?
1. What did you expect to see?
1. What did you see instead?

It also asks how you installed LogSquirl, and the size and kind of the Log File, because a
bug in a 2 GB compressed log rarely shows up in a small plain one. If
LogSquirl crashed and offered to create an issue for you, it has already pre-filled the
version, the platform and the crash id — submit that report and the form's remaining
questions can be answered in a comment.

General questions do not need to follow this checklist.
Feel free to ask anything about using, developing or distributing LogSquirl in the
[Q&A discussions](https://github.com/64x-lunicorn/LogSquirl/discussions/categories/q-a).
Such questions often help to improve project documentation.

# Documentation

LogSquirl has become a quite complex tool with many features. Any time spent fixing
typos or clarifying sections in the documentation is greatly appreciated.
Features that need better documentation can be found in this 
[list](https://github.com/64x-lunicorn/LogSquirl/issues?q=is%3Aissue+label%3A%22status%3A+need+documentation%22+). 
Both open and closed issues marked with label `status: need documentation`
require some work with documentation.

# How to contribute code

Unsure where to begin contributing to LogSquirl? 
You can start by looking through these issues:
- [Good first issues](https://github.com/64x-lunicorn/LogSquirl/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22+sort%3Acomments-desc) -
 issues which should only require a few lines of code.
- [Help wanted issues](https://github.com/64x-lunicorn/LogSquirl/issues?q=is%3Aissue+is%3Aopen+sort%3Acomments-desc+label%3A%22help+wanted%22) -
 issues which should be a bit more involved, required some discussion.

Both issue lists are sorted by total number of comments. While not perfect, number of comments is a reasonable proxy for impact a given change will have.

Working on your first Pull Request? You can learn how from this free series, [How to Contribute to an Open Source Project on GitHub](https://egghead.io/series/how-to-contribute-to-an-open-source-project-on-github).

At this point, you're ready to make your changes! Feel free to ask for help; everyone is a beginner at first :smile_cat:

For something that is bigger than a ten line fix:

1. Create an issue to discuss you idea. It's generally best if you get confirmation
 of your bug fix or approval for your feature
 request this way before starting to code.
1. Create your own fork of the code
1. Do the changes in your fork
1. If you like the change and think the project could use it:
    * Be sure you have followed the code style for the project (.clang-format file is provided; CI checks it with clang-format 23, the same version documented in BUILD.md, so format locally with a matching major version before pushing)
    * Note the [Code of Conduct](CODE_OF_CONDUCT.md).
    * **Run the test suites before creating a pull request** (see below)
    * Create a pull request

## Changelog entry

A pull request with a change users, packagers or plugin authors notice adds an entry to
`CHANGELOG.md`, under a `# Unreleased` heading at the top (add the heading if it is not
there), in the section that fits (`## Changes`, `## Bug fixes`, `## Security`, `## Removed`,
`## Build and packaging`, `## Internal`, `## Documentation`):

```markdown
- **Short title**: What users now see, in present tense.
```

A pull request that needs no entry (CI, tests, refactoring without a visible change, typos)
gets the `no-changelog` label. The **Changelog** check fails until one of the two is done;
Dependabot and Renovate pull requests need neither. The CHANGELOG section of a release
becomes that release's notes on GitHub.

Between a release preparation and the release itself the top section is no longer
`# Unreleased` but the prepared release, for example `# v26.10.0-beta1 (2026-09-17)`. An
entry then goes under a **new `# Unreleased` heading above** that release section — never
into the release section, which is the notes of a release that is being published. The
release-preparation check does not object: it only runs on a pull request that changes the
project version, which an ordinary one does not, so the `no-changelog` label is not needed
for this (#338).

## Testing checklist

Every pull request **must** pass all tests before being merged. Run these locally:

```bash
# 1. C++ unit tests
cd build_root && ctest --build-config RelWithDebInfo --verbose

# 2. E2E integration tests (Python / pytest)
cd tests/e2e
source .venv/bin/activate   # create venv first if not done — see BUILD.md
pytest -v --binary-dir=../../build/output
```

**Performance rule ("Safari Rule"):** LogSquirl must never get slower. The E2E suite
includes performance benchmarks; if your change causes a regression beyond 5 %, the
tests will fail. If you intentionally improve performance, update the baseline:

```bash
pytest -m performance --update-baseline
```

Commit the updated `tests/e2e/baseline.json` with your PR.

## Commit message format
If possible commit message should be like `prefix: message`, where prefix is one of
```
  feat = 'Features',
  fix = 'Bug Fixes',
  docs = 'Documentation',
  style = 'Styles',
  refactor = 'Code Refactoring',
  perf = 'Performance Improvements',
  test = 'Tests',
  build = 'Builds',
  ci = 'Continuous Integration',
  chore = 'Chores',
  revert = 'Reverts',
  tr = 'Translations'
```
    