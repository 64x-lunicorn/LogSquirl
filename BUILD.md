# How to Build LogSquirl

## Overview

These instructions will get you a copy of the project up and running on your local machine for development and testing purposes.
Local builds can be faster because code can be optimized for current CPU instead of generic x86-64. Support for SSE4/AVX code paths
will be enabled if available on build machine.

## Getting the Source

This project is [hosted on GitHub](https://github.com/64x-lunicorn/LogSquirl). You can clone this project directly using this command:

```
git clone https://github.com/64x-lunicorn/LogSquirl
```

## Dependencies

To build LogSquirl:

- cmake 3.12 or later to generate build files
- C++ compiler with C++23 support (at least gcc 13, clang 17, msvc 19.36)
- Qt 6.5 or later (CI builds use Qt 6.11.2):
  - QtCore
  - QtGui
  - QtWidgets
  - QtConcurrent
  - QtNetwork
  - QtXml
  - QtTools
  - Qt5Compat

To build Vectorscan regular expressions backend (default on 64-bit):

- CPU with support for [SSSE3](https://en.wikipedia.org/wiki/SSSE3) instructions (for Vectorscan backend; FAT_RUNTIME auto-selects best SIMD path)
- Boost (1.58 or later, header-only part)
- Ragel (6.8 or later; precompiled binary is provided for Windows; has to be installed from package managers on Linux or Homebrew on Mac)

To build installer for Windows:

- nsis to build installer for Windows
- Precompiled OpenSSl library to enable https support on Windows

Building tests:

- QtTest

All other dependencies are provided by [CPM](https://github.com/cpm-cmake/CPM.cmake) during cmake configuration stage (see 3rdparty directory).

CPM will try to find Vectorscan, TBB, uchardet and xxhash installed on build host.
If a library can't be found, the one provided by CPM will be used.

## Building

### Configuration options

By default LogSquirl is built without support for reporting crash dumps. This can be enabled via cmake option `-DLOGSQUIRL_USE_SENTRY=ON`.

LogSquirl uses Vectorscan regular expressions library which requires CPU with SSSE3 support, ragel and boost headers.
LogSquirl can be built with only Qt regular expressions backend by passing `-DLOGSQUIRL_USE_VECTORSCAN=OFF` to cmake.

Releases are `RelWithDebInfo` builds. LogSquirl optimizes that build type as fully as `Release`
(`-O3` with GCC and Clang, `/Ob2` and a non-incremental `/OPT:REF /OPT:ICF` link with MSVC) and keeps its
debug information for crash reports. Link time optimization is on for every LogSquirl target, not for the
third-party libraries; turn it off with `-DLOGSQUIRL_USE_LTO=OFF`, which makes linking a lot faster during development.

LogSquirl links [mimalloc](https://github.com/microsoft/mimalloc) on every platform. By default only LogSquirl's own
containers, roaring and Vectorscan allocate through it; Qt and the standard containers use the system allocator.
On Linux, `-DLOGSQUIRL_MIMALLOC_OVERRIDE=ON` lets mimalloc serve `malloc` and `new` for the whole process, Qt included.
The option is off until measurements decide it per platform (#282), and configuring fails with it on macOS or Windows:
there a statically linked mimalloc does not take over Qt's allocations.

To measure the override, build the same commit twice, once with the option and once without, and compare the
benchmarks (`tests/benchmarks/README.md`) and the e2e performance suite of both builds. To have the **Benchmarks**
workflow do that on one runner, push a throwaway branch whose only commit turns the option's default to `ON` in
`CMakeLists.txt`, and dispatch the workflow with that branch as `ref` and its parent as `base_ref`:

```bash
gh workflow run benchmarks.yml -f ref=<override-branch> -f base_ref=<branch without it>
```

### Plugin SDK

The plugin C ABI header (`logsquirl_plugin_api.h`) is installed alongside the
application during `cmake --install`. To develop plugins, see
[docs/plugin-sdk.md](docs/plugin-sdk.md) for the complete developer guide.

### Building on Linux

Here is how to build logsquirl on Ubuntu 24.04.

Install dependencies:

```
sudo apt-get install build-essential cmake qt6-base-dev qt6-tools-dev qt6-5compat-dev libboost-all-dev ragel
```

Configure and build logsquirl:

```
cd <path_to_logsquirl_repository_clone>
mkdir build_root
cd build_root
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
cmake --build .
```

**_For Qt 5 builds, replace the qt6 packages above with `qtbase5-dev qttools5-dev`._**

Binaries are placed into `build_root/output`.

See `.github/workflows/ci-build.yml` for more information on build process.

### Building on Windows

Install Microsoft Visual Studio 2022 with C++ support.
Community edition can be downloaded from [Microsoft](https://visualstudio.microsoft.com/vs/).

Install latest Qt 6 version using [online installer](https://www.qt.io/download-qt-installer).
Make sure to select the MSVC 2022 64-bit component.

Install CMake from [Kitware](https://cmake.org/download/).
Use version 3.14 or later for Visual Studio 2022 support.

Download the Boost source code from http://www.boost.org/users/download/.
Extract to some folder. Directory structure should be something like `C:\Boost\boost_1_63_0`.
Then add `BOOST_ROOT` environment variable pointing to main directory of Boost sources so CMake is able to fine it.

Prepare build environment for CMake. Open command prompt window and run:

```
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\vsdevcmd" -arch=x64
```

Next setup Qt paths:

```
<path_to_qt_installation>\bin\qtenv2.bat
```

Then add CMake to PATH:

```
set PATH=<path_to_cmake_bin>:$PATH
```

Configure logsquirl solution:

```
cd <path_to_project_root>
md build_root
cd build_root
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=RelWithDebInfo ..
```

CMake should generate `logsquirl.sln` file in `<path_to_project_root>\build_root` directory. Open solution and build it.

Binaries are placed into `build_root/output`.

For https network urls support download precompiled OpenSSL 3.x library from https://www.firedaemon.com/firedaemon-openssl.
Put libcrypto-3 and libssl-3 for desired architecture near logsquirl binaries.

### Building on Mac OS

LogSquirl requires macOS 15 (Sequoia) or higher.

Install [Homebrew](https://brew.sh/) using terminal:

```
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

Homebrew installer should also install xcode command line tools.

Download and install build dependencies:

```
brew install cmake ninja qt boost ragel
```

Usually path to qt installation looks like `/opt/homebrew/opt/qt/lib/cmake/Qt6` (Apple Silicon) or `/usr/local/opt/qt/lib/cmake/Qt6` (Intel).

Configure and build logsquirl:

```
cd <path_to_logsquirl_repository_clone>
mkdir build_root
cd build_root
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DQt6_DIR=<path_to_qt_install> ..
cmake --build .
```

Binaries are placed into `build_root/output`.

By default, logsquirl will rely on cmake to figure out target MacOS version. Usually it uses build host version.
To override default cmake value pass an option `-DLOGSQUIRL_OSX_DEPLOYMENT_TARGET=<target>` to cmake during configuration step,
`<target>` is one of `14`, `15`, `16`. LogSquirl's target must be greater or equal to the target used by Qt libraries.

## Code style

Formatting follows the `.clang-format` file at the repository root. CI runs a
"Format" job (clang-format in dry-run mode over every project-owned `.cpp`,
`.h` and `.hpp` file) pinned to **clang-format 23** — the config's
`Standard: c++23` requires that major version. Format locally with a
matching version before pushing:

```bash
pip install --require-hashes -r .github/requirements/clang-format.txt
clang-format -i <file>
```

## Running tests

### C++ unit tests (Catch2)

Tests are built by default. To turn them off pass `-DLOGSQUIRL_BUILD_TESTS=OFF` to cmake.
Tests use Catch2 (bundled with logsquirl sources) and require QtTest module. Tests can be run using ctest tool provided by CMake:

```
cd <path_to_logsquirl_repository_clone>
cd build_root
ctest --build-config RelWithDebInfo --verbose
```

Each Catch2 test case is its own ctest test named `<test executable>: <test case>`, so a
single case runs with e.g. `ctest -R "^logsquirl_tests: Scenario: QuickFind"`. The tests
run one after another: the Qt test executables share one portable settings file.

### Theme screenshots

A hidden UI test renders every Theme (Light, Dark, High Contrast) to PNG files: the main window
with a Log File and a Search, the sidebar, every menu, the Command Palette, the dialogs and a
gallery of every standard widget in every state. It is not part of `ctest` or CI; run it by its
tag, offscreen (no display or screen-recording permission needed), into a directory of your choice:

```
LOGSQUIRL_SCREENSHOT_DIR=/path/to/shots build/output/logsquirl_itests -platform offscreen "[.screenshots]"
```

Each image is named `<view>_<theme>.png`, so the Themes of one view sort together. Render once
before and once after a Theme change into two directories to compare them side by side. The Log
File shown is a copy of `test_data/screenshot_demo.txt` under `/tmp/logsquirl-screenshots`, so no
path of your machine appears in the images.

### E2E integration tests (Python / pytest)

End-to-end tests exercise the compiled `logsquirl_grep` and `logsquirl` binaries
against the files in `test_data/`. They cover search correctness, encoding handling,
edge cases, GUI smoke tests, and **performance regression detection** (5 % tolerance).

**Prerequisites:** Python >= 3.10

```bash
# One-time setup (from repository root)
cd tests/e2e
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -e .

# Run all E2E tests (40 tests)
pytest -v --binary-dir=../../build/output

# Run only functional tests (skip performance benchmarks)
pytest -v --binary-dir=../../build/output -m "not performance"

# Run only performance benchmarks
pytest -v --binary-dir=../../build/output -m performance
```

**Performance baselines:** After optimizations, update the baseline with
`pytest -m performance --update-baseline`. Review the diff in `baseline.json`
before committing — values should only go down, never up.

See [`tests/e2e/README.md`](tests/e2e/README.md) for full documentation.

## CI/CD Pipeline

### Version Numbering

CI builds use the scheme `YY.MM.PATCH.BUILD`.

- `YY.MM.PATCH` is the `project(... VERSION …)` value declared in
  `CMakeLists.txt`, for every build (pull requests, branch dispatches and
  pushes to master alike).
- `BUILD = github.run_number + 717`, the CI Build run number.

For example, `VERSION 26.06.1` on CI Build run number 50 produces version
`26.06.1.767`. `.github/actions/logsquirl-version` resolves it.

A release publishes the packages CI Build made for the tagged commit on master;
the tag is pushed after that build, so the commit has to declare the release's
version already. Before tagging `vX.Y.Z` (or a pre-release `vX.Y.Z-betaN`), set
`VERSION X.Y.Z` in `CMakeLists.txt` on master and let CI Build pass. CI Release
refuses a build whose base version is not the tag's, naming both.

The same commit must also carry the release's section in `CHANGELOG.md`, headed
with the tag itself and the date, e.g. `# v26.10.0-beta1 (2026-09-17)`: that
section is the release's notes. CI Release fails before anything is signed when it is missing.

### Docker Build Containers

Linux builds use pre-built Docker images hosted on GHCR:

| Image | Based On | Packages |
|-------|----------|----------|
| `ghcr.io/64x-lunicorn/logsquirl-oracle10` | Oracle Linux 10 | Qt 6, GCC, RPM |
| `ghcr.io/64x-lunicorn/logsquirl-ubuntu-noble` | Ubuntu 24.04 | Qt 6, GCC, DEB |
| `ghcr.io/64x-lunicorn/logsquirl-ubuntu-jammy` | Ubuntu 22.04 | Qt 6, GCC 12, AppImage |
| `ghcr.io/64x-lunicorn/logsquirl-fedora44` | Fedora 44 | Qt 6, GCC, RPM |

Every image is built with `docker/` as its build context, so all four install sccache
from the one script `docker/shared/install-sccache.sh`; bumping sccache is an edit to that file only.
Images are content-addressed. `docker/image-hash.sh` hashes an image's own directory and `docker/shared`;
the **Docker Images** workflow pushes each image under that hash as its tag (plus `:latest`, for humans only)
and never overwrites an existing hash tag. CI Build computes the same hash from its checkout and pulls exactly
`<image>:<hash>`; when that tag does not exist (a PR that changes `docker/`, or a merge the workflow has not
published yet) it builds the image locally under the same ref. So an open PR's toolchain changes only when
its own `docker/` files do.

The **GHCR Cleanup** workflow (`ghcr-cleanup.yml`, weekly) deletes the image versions no run uses any more: images
of an earlier hash that are older than 30 days, the old commit SHA tags, and the signatures and manifests that
belong to them. It keeps `:latest`, the hashes of master's `docker/` directories and anything younger than two days.
A deleted image a PR still asks for is built locally, as for any new hash. Dispatch it with *dry run* (the default)
to see what it would delete.

Before pushing, the Docker Images workflow scans each image with Trivy (CRITICAL and HIGH, fixed upstream only) and
uploads the result to code scanning under `trivy-image-<name>`; findings are reported there but do not fail the
build. The pushed image carries buildx SBOM and provenance attestations and a keyless cosign signature. CI Build
refuses a pulled image whose signature does not come from `ci-docker.yml` on master; to check one by hand:

```sh
cosign verify ghcr.io/64x-lunicorn/logsquirl-ubuntu-noble:<hash> \
  --certificate-identity-regexp '^https://github\.com/64x-lunicorn/LogSquirl/\.github/workflows/ci-docker\.yml@refs/heads/master$' \
  --certificate-oidc-issuer https://token.actions.githubusercontent.com
```

Every `FROM` is pinned by digest (`image:tag@sha256:…`); Dependabot proposes digest bumps as pull requests.

Images are published when files in `docker/` change on master. OS security patches arrive through a monthly
scheduled run that bumps `docker/shared/refresh-stamp` on the branch `ci/docker-image-refresh` and files an issue
linking to it; opening and merging that pull request gives every image a new hash and so a fresh build. Workflows
may not open pull requests in this repository, so the maintainer opens it, and CI Build runs on it as usual. To
propose a refresh by hand, run the **Docker Images** workflow via `workflow_dispatch` with *propose_refresh*;
running it on master without that publishes any hash tag still missing.

> **AppImage compatibility:** The AppImage is built on the Ubuntu 22.04 (jammy)
> image on purpose. `linuxdeploy` bundles Qt and libssl but never bundles glibc
> or libstdc++ — those come from the host — so the build host's C library sets
> the compatibility floor. Building on jammy pins that floor to glibc 2.35 and
> the GCC 12 libstdc++ (`GLIBCXX_3.4.30`), letting the AppImage run on Ubuntu
> 22.04 and every newer distribution. Do not move the AppImage build to a newer
> base unless you intend to drop support for older distros. (The `.deb` is still
> produced on Ubuntu 24.04 and targets that release and newer.)

### Release Process

A release is prepared in one pull request that sets the version in `CMakeLists.txt`, turns `# Unreleased` in
`CHANGELOG.md` into the release's section, adds the release's `changelog` entry to `latest.json` and its page on
the website (*Release pages on the website*). CI Build's Format job checks a pull request that changes the version
for all of these (`.github/scripts/release-prep.py release-preparation`) and names every missing piece.

Releases are triggered by pushing a git tag to a master commit whose CI Build
push run succeeded, whose `CMakeLists.txt` declares the tag's version and whose
`CHANGELOG.md` has the tag's section (see *Version Numbering*):

- **Stable release**: push a semver tag like `v26.04.0`
- **Beta release**: push a pre-release tag like `v26.04.0-beta1`

The release workflow does not build. It:
1. Finds the successful CI Build run for the push to master that built the
   tagged commit and fails with an error if there is none (still running,
   failed, or never run because the commit was `[skip ci]` or only touched
   ignored paths). It downloads that run's packages by artifact ID, checks each
   against its digest, and checks that the build is the tag's: every artifact
   belongs to that run and commit, the SBOM records the commit, and the version
   file, the SBOM, the Linux package names, `logsquirl_portable.exe` in the
   Windows portable zip and the macOS app (its binary and `CFBundleVersion`)
   carry the tag's version (`.github/scripts/release-build.py`)
2. Signs the macOS app, notarizes and staples it, packs the DMG again from it
   and signs, notarizes and staples the DMG
   (`.github/actions/mac-sign-notarize`), in the `release` environment
3. Uploads debug symbols to Sentry (non-blocking), in the `release` environment
4. Builds the release SBOM `logsquirl-<version>-sbom.cdx.json` (CycloneDX 1.6):
   the CPM packages and pinned platform components that CI Build's SBOM job read
   from the built commit, plus the Qt, OpenSSL and ICU versions found in the
   AppImage, Windows zip and macOS app and what syft finds in them
   (`scripts/sbom/logsquirl_sbom.py`)
5. Scans the SBOM for known vulnerabilities (`scripts/sbom/logsquirl_vulns.py`):
   grype for the components with a CPE, OSV for the CPM packages by pinned commit
   and tag, and Qt's own list of advisories (https://wiki.qt.io/List_of_known_vulnerabilities_in_Qt_products,
   "Qt Framework" section) for the Qt version, with the severity NVD gives the CVE.
   The scan fails as a tooling error when that page can no longer be read; an advisory whose affected versions
   it cannot read is reported as unconfirmed and does not block. NVD requests are retried on rate limits and
   timeouts; if NVD still cannot be reached for a matched Qt advisory, the release scan fails as a tooling error
   (the daily scan only warns). All findings go to code scanning (category `sbom-vulns`); a critical one (CVSS
   v3/v4 base score ≥ 9.0 or rated critical) stops the release unless `scripts/sbom/vuln-ignore.yml` on master
   accepts it with a reason and an expiry date. So does a Qt advisory NVD has not scored yet (reported as
   *unscored*): assess it and record the decision in the ignore file.
   Without an NVD API key the scanner is limited to 5 NVD requests in 30 seconds. To raise it to 50, request a free
   key at https://nvd.nist.gov/developers/request-an-api-key (it arrives by e-mail and is activated from the link
   in it), then add it as the repository secret `NVD_API_KEY` (Settings → Secrets and variables → Actions, or
   `gh secret set NVD_API_KEY`). The release and daily scans pass it to `.github/actions/sbom-vuln-scan`; without
   the secret they run unkeyed. The file is read
   from master even for a tag release, so accepting a risk and re-running the failed job is enough.
   The `Vulnerability scan` workflow scans master's source SBOM daily and only reports.
6. Creates a draft GitHub Release with all platform packages, the SBOM and the checksum file, and as its notes
   the tag's `CHANGELOG.md` section followed by how to verify the downloads,
   attests build provenance for every asset and the SBOM for every other asset, signs the checksum file keyless with
   cosign (the `.sigstore.json` bundle is uploaded as an asset but is not listed in
   the checksum file), then publishes the draft. A failure in between leaves a draft.
7. Commits the release to the update feed `latest.json` on the branch `feed/<tag>` and names the link to open
   its pull request (job summary and a notice). The ruleset only lets a pull request with passing checks change
   master, and workflows may not open pull requests here, so **the maintainer opens and merges it**; the app
   announces the release once it is merged. The Changelog check needs no entry for a `feed/` branch.
8. Dispatches **Deploy Website**, so the release's page goes live (see *Release pages on the website*)

`latest.json` on master is the update feed LogSquirl reads at start-up
(`src/versioncheck`). Its fields:

| Field | Written by | Used for |
|-------|------------|----------|
| `stable`, `stable_url`, `stable_build` | CI Release (feed pull request), stable tag | The latest stable release: its name, release page and the `YY.MM.PATCH.BUILD` it was published from |
| `beta`, `beta_url`, `beta_build` | CI Release (feed pull request), pre-release tag | The latest beta, offered to users with "check for beta versions" on and to users running a beta |
| `releases` | CI Release (feed pull request) | Every published release name; a running version that was only published as betas runs a beta |
| `changelog` | Release preparation (by hand, oldest first) | One line per release, listed in the update notification for the releases a user skips, up to the offered one |
| `ci`, `ci_url` | CI Release (feed pull request), stable tag (`ci` only) | Read only by LogSquirl 26.07.0 and older, which append an OS suffix to `ci_url`; it ends in `#`, so they land on the latest release page |
| `stable_version`, `beta_version` | CI Release | Not read by the application |

A release is offered when its build is newer than the running one; betas and
the stable release of a version share `YY.MM.PATCH` and differ only in the
build. Without a `*_build` field only a newer `YY.MM.PATCH` is offered. A
re-run of an older release leaves a feed that announces a newer build unchanged.

#### Release pages on the website

A release's page is `website/src/content/docs/news/release-YY-MM.md`, and its frontmatter is the only place
the website writes the release's version:

```yaml
release:
  version: 26.10.0-beta1   # the tag without its "v"
  date: 2026-09-17
  channel: beta            # stable, beta or legacy
```

The sidebar, the release overview and the home page's latest release cards are generated from these pages
(`website/src/releases.mjs`); a missing or malformed field fails the website build. The page is merged with
the release preparation, but **Deploy Website** leaves out every page whose release has no published GitHub
release yet, so it goes live when CI Release dispatches the deploy after publishing. `npm run dev` and the
pull request build show every page.

Manual releases, e.g. to re-run a release, are also supported via
`workflow_dispatch`: dispatch it from the tag (*Use workflow from*, or
`gh workflow run ci-release.yml --ref v26.04.0 -f tag=v26.04.0`) with that tag
as input. The optional CI Build run ID must name the successful push run on
master for the tagged commit; without it, that run is found as for a tag push.

#### Secrets and environments

The signing and upload secrets are not repository secrets but secrets of GitHub
Environments, so only the jobs bound to an environment can read them, and only
for the refs its deployment policy admits:

| Environment | Deployment policy | Secrets | Jobs |
|-------------|-------------------|---------|------|
| `release` | tags `v*` | `MACOS_P12_FILE`, `MACOS_P12_PASSWORD`, `APPLE_ID`, `APPLE_PASSWORD`, `APPLE_TEAM_ID`, `SENTRY_TOKEN` | CI Release `sign-mac`, `sentry` |
| `website` | branch `master` | `FTP_SERVER`, `FTP_USERNAME`, `FTP_PASSWORD` | Deploy Website `deploy` |

CI Build never signs and references no signing secret: a pull request, a push
to master and a `workflow_dispatch` on any branch all produce the same unsigned
macOS and Windows packages. A manual CI Release dispatched from a branch fails
before anything is downloaded, because its signing job could not enter the
`release` environment.

### Workflows

| Workflow | Trigger | Purpose |
|----------|---------|---------|
| `ci-build.yml` | push/PR to master | Build + test all platforms, check the update feed; on a pull request also check a release preparation and build the website with its link check |
| `changelog.yml` | PR to master (also on label changes) | Require a CHANGELOG entry under `# Unreleased`, or the `no-changelog` label |
| `deploy-website.yml` | push to master changing `website/**`, dispatch (also by CI Release) | Build the website without the pages of unpublished releases and upload it |
| `ci-release.yml` | tag push `v*` | Sign and publish the CI Build packages of the tagged commit as a GitHub Release |
| `ci-docker.yml` | `docker/**` changes | Build + push Docker images to GHCR |
| `ghcr-cleanup.yml` | weekly schedule, dispatch | Delete the build image versions on GHCR that no CI run uses any more |
| `renovate-checksums.yml` | PR from a `renovate/*` branch | Recompute the SHA-256 of every pinned download after a Renovate version bump |
| `codeql-analysis.yml` | push/PR + weekly schedule | CodeQL security analysis of the C++ code and the workflows; results in third-party code (`build/_deps`, `cpm_cache`) are dropped before upload, because `paths-ignore` has no effect for compiled languages |


Every pull request runs CI Build, so the required **CI passed** check always reports. Its *Changes* job
skips the build, test and SBOM jobs for a pull request that changes only the files a push to master ignores
(`website/**`, `latest.json`, `BUILD.md`, `README.md`, `CONTRIBUTING.md`, `CODE_OF_CONDUCT.md`, `.gitignore`), and
runs the *Website* job only when the website changed: `npm test`, then `npm run build` (which fails on a broken
internal link) twice, as is and as deployed without the pages of unpublished releases. The *Format* job checks the
update feed on every run (`.github/scripts/release-feed.py check`).

### Action pinning

Every third-party action in `.github/workflows/` and `.github/actions/` is pinned to the full commit SHA of a
release, with that release's exact version as a comment; local actions (`./.github/actions/...`) are exempt:

```yaml
- uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
```

A tag can be moved to other code, a commit SHA cannot. Dependabot reads the `# vX.Y.Z` comment and bumps SHA and
comment together. To add or bump an action by hand, resolve the release tag to its commit (peel an annotated tag:
`gh api repos/<owner>/<repo>/git/ref/tags/<tag>`, then `.../git/tags/<sha>` while the object type is `tag`). The
repository requires SHA pinning, and the Format job of CI Build runs the same check as

```bash
.github/scripts/check-action-pins.sh
```

### Dependency updates

Two bots propose dependency updates as pull requests, each for what the other cannot read, so no dependency gets
PRs from both:

- **Dependabot** (`.github/dependabot.yml`): the GitHub Actions `uses:` pins, the digest-pinned Docker `FROM` lines
  and the website's npm packages.
- **Renovate** (`renovate.json5`, only its custom regex managers and pip-compile are enabled): the CPM packages in
  `3rdparty/CMakeLists.txt`, every tool version pinned in workflows, composite actions, the build images and the
  packaging scripts (Qt, OpenSSL, Boost, Ninja, CMake, Ragel, sccache, grype, NSIS, create-dmg, sentry-cli,
  linuxdeploy, the aqtinstall commit of `install-qt-action` and the Renovate config validator itself), the digests of
  CI Build's install-check images (`check_container`), and the hash-locked pip requirements.

**pip requirements.** Every `pip install` in CI and the build images reads a requirements file with exact versions and
hashes and passes `--require-hashes`: `docker/shared/aqtinstall-requirements.txt` (aqtinstall, installed into a
throwaway directory that is deleted once Qt is in the image), `.github/requirements/clang-format.txt`,
`.github/requirements/e2e.txt` and `scripts/sbom/requirements.txt`. Each is generated from the `.in` file next to it by
the `uv pip compile --generate-hashes --universal` command in its header; after editing a `.in` file, rerun that
command. Renovate bumps the pins and reruns the command, and re-locks the dependencies below them once a month.

Both wait until a release is seven days old and run weekly; Renovate lists everything it tracks on its
**Dependency Dashboard** issue. Renovate's grouping:

- one PR per dependency, with a major version in a PR of its own;
- **Qt** in one PR across the CI matrices, CodeQL, the four Dockerfiles and this file (the SBOM job fails when they differ);
- **build tools** (CMake, Ninja, Ragel, sccache) in one PR, since a bump in `docker/` rebuilds every build image;
- **linuxdeploy** and its Qt plugin together;
- a CPM package pinned to a release updates `VERSION`, the commit SHA and the `# <tag>` comment in one change;
- a CPM package without releases (forks such as `variar/oneTBB`, `KDAB/KDToolBox`, `getsentry/sentry-native`) tracks
  its default branch and only gets a PR once its checkbox on the Dependency Dashboard is ticked.

A tool pin that is downloaded and verified is written as a block Renovate and the checksum script both read; to add
one, follow the same form and add its download URL to `URLS` in `.github/scripts/update-checksums.py`. The block goes
into a composite action (`.github/actions/*/action.yml`), a Dockerfile or a script, never into a workflow file: the
Renovate Checksums workflow pushes with `GITHUB_TOKEN`, which may not change `.github/workflows/`, so
`update-checksums.py --list` fails on a pair there. That is why OpenSSL (`.github/actions/windows-openssl`) and
sentry-cli (`.github/actions/install-sentry-cli`) are installed by composite actions:

```yaml
# renovate: datasource=github-releases depName=anchore/grype
GRYPE_VERSION: 0.118.0
GRYPE_SHA256: 1d444c5e…
```

**Checksums.** Renovate's hosted app cannot download a release and hash it, so its PR changes the version and
leaves the SHA-256 stale. The **Renovate Checksums** workflow runs on every PR from a `renovate/*` branch, recomputes
the hash of each pair whose version differs from the PR's base branch and pushes a commit with the corrected hashes.
A hash that no longer matches a version the PR did not change is never rewritten: the run fails naming the
dependency and URL, since the release's bytes changed under the same version and need a look first. GitHub starts no workflows for a
push made with `GITHUB_TOKEN`, so **close and reopen the PR** after that commit appears to run CI Build on it. The
new hashes are what the URL served at that moment: where upstream publishes checksums (grype, CMake, Boost), compare
before merging. Renovate stops rebasing a branch someone else has pushed to; tick *rebase* on the PR to get a fresh
one (the workflow then fixes the hashes again). Locally:

```bash
.github/scripts/update-checksums.py --list                 # parse only: every pair has a URL rule (the Format job runs this)
.github/scripts/update-checksums.py --check                # download and verify every hash
.github/scripts/update-checksums.py --base origin/master   # rewrite the hashes of versions changed against master
```

The Format job also runs `renovate-config-validator` on `renovate.json5`.

**Setting it up.** Install the [Renovate GitHub App](https://github.com/apps/renovate) for this repository only.
Because `renovate.json5` already exists, Renovate skips its onboarding PR; on its first scheduled run it opens the
Dependency Dashboard issue and the PRs for everything already outdated (for example Catch2, xxHash, mimalloc,
simdutf, CMake). Merge them one at a time, closing and reopening each PR once the checksum commit is on it.

### Repository settings

Some guarantees live in the repository settings rather than in a workflow file: `GITHUB_TOKEN` is read-only unless a
job asks for more, workflows cannot create or approve pull requests, only GitHub-owned actions and an explicit list
of third-party actions may run, SHA pinning is required, and `v*` tags can only be created by an admin and never
moved or deleted. `.github/scripts/repo-settings.sh` holds that list and both checks and applies it (admin `gh` login
needed):

```bash
.github/scripts/repo-settings.sh check   # exit 1 on drift
.github/scripts/repo-settings.sh apply
```

`--defer-sha-pinning` (for both) leaves required SHA pinning untouched. Use it while master still has workflows with
unpinned actions, since requiring pins fails every such run.

A new third-party action has to be added to the script's `ALLOWED_ACTIONS` and applied before the workflow using it
can run. The allowlist also applies to actions that other actions call internally (for example `aquasecurity/trivy-action`
runs `aquasecurity/setup-trivy`), and `owner/repo@*` does not cover a subdirectory such as
`jurplel/install-qt-action/action`. `.github/scripts/check-action-allowlist.py` follows every `uses:` into the
referenced actions at their pinned commits and fails with the missing pattern; the Workflow Security workflow runs it on
every pull request that touches `.github/`, and it runs locally with any `gh` login. Because workflows cannot create `v*` tags, push the release tag before dispatching CI Release by hand.
