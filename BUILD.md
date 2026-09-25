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

- cmake 3.16 or later to generate build files
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
Such a build downloads the pinned [minidump-stackwalk](https://github.com/rust-minidump/rust-minidump) release for
the target (Linux x86-64, macOS arm64/x86-64, Windows x64) at configure time, checks its SHA-256
(`cmake/MinidumpStackwalk.cmake`) and ships it next to the app as `logsquirl_minidump_dump`; the crash report dialog
runs it on a pending minidump. `-DLOGSQUIRL_MINIDUMP_STACKWALK=<path>` ships an existing executable instead, for
offline builds or other targets.

LogSquirl uses Vectorscan regular expressions library which requires CPU with SSSE3 support, ragel and boost headers.
LogSquirl can be built with only Qt regular expressions backend by passing `-DLOGSQUIRL_USE_VECTORSCAN=OFF` to cmake.

On Windows, a build with `-DLOGSQUIRL_GENERIC_CPU=ON` (the release) builds Hyperscan twice, as `hs.dll` for SSE4.2
and `hs_avx2.dll` with `/arch:AVX2`, and loads the one the CPU supports at the first Search. Setting the environment
variable `LOGSQUIRL_HYPERSCAN_DISABLE_AVX2=1` makes it load `hs.dll` on a CPU with AVX2 too. Without the option,
Hyperscan is linked statically and built for the build machine's CPU, like the rest of LogSquirl.

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
`CMakeLists.txt`, and dispatch the workflow from that branch with its parent as `base_ref`:

```bash
gh workflow run benchmarks.yml --ref <override-branch> -f base_ref=<branch without it>
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
Tests use Catch2 v3 (fetched and built by CMake, pinned in `3rdparty/CMakeLists.txt`) and require QtTest module. Tests can be run using ctest tool provided by CMake:

```
cd <path_to_logsquirl_repository_clone>
cd build_root
ctest --build-config RelWithDebInfo --verbose
```

Each Catch2 test case is its own ctest test named `<test executable>: <test case>`, so a
single case runs with e.g. `ctest -R "^logsquirl_tests: Scenario: QuickFind"`. The tests
run one after another: the Qt test executables share one portable settings file.

### Theme screenshots

A hidden UI test renders every Theme (Light, Dark, High Contrast, Smyck, Smyck Light) to PNG files:
the main window with a Log File and a Search, the sidebar, every menu, the Command Palette, the
dialogs and a gallery of every standard widget in every state. It is not part of `ctest` or CI; run
it by its tag, offscreen (no display or screen-recording permission needed), into a directory of
your choice:

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

### Fuzzing

`tests/fuzz` holds libFuzzer targets for the code that reads bytes from anywhere before a user sees
anything: `indexing_blocks_fuzzer` (indexing blocks and stitching, in every line feed width),
`log_format_fuzzer` (Log Format JSON parser and field extractor) and `ansi_color_fuzzer`. Their seed
inputs are in `tests/fuzz/corpus/<target>/`. CI runs them with ClusterFuzzLite
(`.github/workflows/cflite.yml`, `.clusterfuzzlite/`): on pull requests for the changed code, weekly for
every target.

To run them locally you need Clang with libFuzzer (on macOS Homebrew's `llvm`, not Apple's Clang). The whole
build must be instrumented with `-fsanitize=fuzzer-no-link`, or the fuzzers see no coverage of the libraries:

```bash
F="-fsanitize=address,undefined,fuzzer-no-link"
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_C_FLAGS="$F" -DCMAKE_CXX_FLAGS="$F" \
  -DLOGSQUIRL_BUILD_FUZZERS=ON -DLOGSQUIRL_BUILD_TESTS=OFF \
  -DLOGSQUIRL_USE_LTO=OFF -DLOGSQUIRL_USE_VECTORSCAN=OFF
cmake --build build-fuzz --target logsquirl_fuzzers
mkdir -p /tmp/corpus && cp -r tests/fuzz/corpus/ansi_color/. /tmp/corpus/
build-fuzz/output/ansi_color_fuzzer /tmp/corpus -max_total_time=60
```

A crash is written as `crash-<hash>`; run the target with that file as its only argument to reproduce it.
Copy a small input that found new code into `tests/fuzz/corpus/<target>/` to keep it as a seed.

### Sanitizer builds

`cmake/Sanitizers.cmake` adds Address, Memory, Undefined Behavior and Thread sanitizers behind their own
options (GCC/Clang only): `-DENABLE_SANITIZER_ADDRESS=ON`, `-DENABLE_SANITIZER_MEMORY=ON`,
`-DENABLE_SANITIZER_UNDEFINED_BEHAVIOR=ON`, `-DENABLE_SANITIZER_THREAD=ON`. Measure with `RelWithDebInfo` and
`-DLOGSQUIRL_USE_LTO=OFF`; LTO makes linking a sanitizer build a lot slower for no measurement benefit.

```bash
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLOGSQUIRL_USE_LTO=OFF -DENABLE_SANITIZER_THREAD=ON ..
cmake --build .
ctest --build-config RelWithDebInfo --verbose
```

**ThreadSanitizer baseline (#347).** Both oneTBB's flow graph (used by indexing and search) and one
finding inside uninstrumented `QtCore` (`QThreadPoolThread::run()`, reported against the
`shared_ptr<const RegularExpression>` that `LogFilteredDataWorker::search()` hands to its worker thread)
are findings in code TSan cannot instrument, not races in LogSquirl's own logic; see
`docs/adr/0007-tsan-suppresses-onetbb-and-uninstrumented-qt-internals.md` for how each was investigated
and, for the `shared_ptr` finding, why it is judged safe rather than merely suppressed. `cmake/tsan.supp`
lists both, each with its reasoning as a comment.

`ctest` picks the suppression file up automatically (`cmake/CatchTestDiscoveryRunTest.cmake` sets
`TSAN_OPTIONS=suppressions=cmake/tsan.supp` for every test case; harmless for a non-TSan build, since
`TSAN_OPTIONS` is then simply unread). Running a TSan binary directly, outside ctest, needs the same
option by hand:

```bash
TSAN_OPTIONS="suppressions=$(pwd)/cmake/tsan.supp" build_root/output/logsquirl_tests
```

**TSan in CI (#439).** The `Sanitizers / tsan` job in `.github/workflows/ci-build.yml` builds the same
configuration as above in the Noble container and runs every test case under `ctest`. It runs on every
push to master and by hand (`workflow_dispatch`), not on pull requests, and does not fail the run
(`continue-on-error`) yet: its first run over the whole suite reported 912 races and turned 78 of about 740
test cases red, mostly in oneTBB and Qt internals and in Search code. Once those are triaged (#482) it joins
the pull requests and blocks, like `Sanitizers / asan-ubsan`. It needs no `TSAN_OPTIONS` of its own: a suppression added to
`cmake/tsan.supp` reaches it through `cmake/CatchTestDiscoveryRunTest.cmake`. Runtime: expect it to take
about as long as the ASan/UBSan job (the ASan job's slowest successful run is 25 minutes), because TSan
slows the tests down by a similar factor; the first runs of the job will give the real number, which
belongs here. To see the job go red, add a plain `int` incremented from two `std::thread`s to any test
case: TSan reports it, the case exits non-zero, and so does `ctest`.

With the suppression file applied, most but not all of the search tests pass; the ADR above records which
findings remain and why they are not yet covered, rather than a suppression widened to hide them.

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

> **deb and rpm packages need the distribution's Qt:** unlike the AppImage, the
> `.deb` and `.rpm` packages do not bundle Qt. They are linked against the Qt of
> the build image (aqtinstall, `QT_VERSION` in `docker/*/Dockerfile`) and declare
> the distribution's Qt packages with that version as the minimum
> (`CPACK_DEBIAN_PACKAGE_DEPENDS` / `CPACK_RPM_PACKAGE_REQUIRES` in
> `CMakeLists.txt`, taken from the Qt CMake found). On a distribution whose Qt is
> older, the package manager refuses to install the package; use the AppImage
> there. CI Build's package check (`packaging/linux/check-package.sh`) verifies in
> a plain container of each target distribution that the package ships no static
> libraries, headers (except the Plugin SDK header) or CMake files of its
> dependencies, that it declares the Qt dependency, and that it installs exactly
> when the distribution's Qt is new enough; otherwise the job logs a warning.

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
   (`scripts/sbom/logsquirl_sbom.py`). The Ubuntu 22.04 system libraries the
   AppImage bundles carry no version syft can read: `generate_appimage.sh` asks
   the build image's dpkg database for the package and version of each one
   (`logsquirl_sbom.py appimage-debs`, written to `logsquirl_appimage_debs.json`
   in the AppImage artifact), and the SBOM lists those packages with
   `pkg:deb/ubuntu/...` purls, which grype matches against the Ubuntu security
   tracker. The file itself is not a release asset
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
   The `Vulnerability scan` workflow scans master's source SBOM daily and only reports. It also reconciles the
   code scanning alerts with the ignore file, which code scanning would otherwise not act on: an accepted
   finding's alert is dismissed as *won't fix* with the recorded reason, and reopened once the entry expires.
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
9. For a stable release, sets the Homebrew cask to it, checks that it installs and pushes it to the tap, in the
   `release` environment (see *Homebrew tap*)

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

**The website goes live with a release, not with a merge.** Deploy Website has no push trigger: a website
change merged to master waits for the next release, which is when CI Release dispatches the deploy and the
whole site, including that release's page, goes up at once. Until then the change is only visible in
`npm run dev` and in the pull request build, whose link check is what keeps a broken website out of master.

Dispatching the workflow by hand stays the way to deploy without cutting a release, from the Actions tab or
with `gh workflow run deploy-website.yml --ref master`. Two cases need it: a website fix that cannot wait for
the next release (a wrong download link, legal text), and a release whose deploy did not run or failed, which
leaves the site on the previous release until someone dispatches it. Nothing retries that on its own, and the
dispatch must be on `master`, because the `website` environment admits no other branch.

Manual releases, e.g. to re-run a release, are also supported via
`workflow_dispatch`: dispatch it from the tag (*Use workflow from*, or
`gh workflow run ci-release.yml --ref v26.04.0 -f tag=v26.04.0`) with that tag
as input. The optional CI Build run ID must name the successful push run on
master for the tagged commit; without it, that run is found as for a tag push.

#### Homebrew tap

macOS users on Apple Silicon (macOS 15 or later; there is no Intel build) can install LogSquirl with
`brew install --cask 64x-lunicorn/tap/logsquirl`. The cask is
`Casks/logsquirl.rb` in [`64x-lunicorn/homebrew-tap`](https://github.com/64x-lunicorn/homebrew-tap), a repository
of its own because only a repository named `homebrew-*` can be tapped by that short name. The official
`homebrew/cask` does not accept LogSquirl yet (its notability threshold); once it does, the cask moves there.

The app does not update itself, so the cask declares no `auto_updates` and `brew upgrade` is how cask users get a
new release. That makes a stale cask a stale install, so CI Release's `update-homebrew` job sets `version` and
`sha256` in the cask after every stable release (never a beta, which would reach every cask user):

1. It takes the hash from the `logsquirl-mac-arm64.dmg` line of the release's checksum file, which lists the signed
   DMG as published (`.github/scripts/release-cask.py`). A cask that already has the release and hash, or a newer
   release, stays as it is: a re-run pushes nothing and an older release never downgrades the cask.
2. It commits the change in its clone of the tap, taps that clone, runs `brew audit --cask --strict --online`,
   installs the cask, checks that the installed app's `CFBundleVersion` is the release's version and uninstalls it
   again, on a macOS runner and without launching the app. The audit checks URL and hash, not that the DMG holds
   the app the cask names; the install does.
3. Only then it pushes the commit to the tap's `main`, through the deploy key `HOMEBREW_TAP_DEPLOY_KEY` (a secret of
   the `release` environment with write access to `homebrew-tap` alone). The tap has no required checks, so unlike
   the update feed there is no pull request.

The job runs after the release is published: when it fails, the release is out and only the cask lags behind. Fix
the cause and re-run the job, or update the cask by hand the same way (`release-cask.py update`, then audit and
push). Releases up to 26.07.0 have no DMG line in their checksum file; hash their download with `shasum -a 256`.

Removing the tap, for instance once the cask moves to `homebrew/cask`, means removing three things together: the
`update-homebrew` job, the tap's deploy key and the `HOMEBREW_TAP_DEPLOY_KEY` secret.

#### Package repository

Ubuntu 24.04 (amd64) users add the LogSquirl APT repository once (README) and get every release with
`apt upgrade`. It is served at `https://packages.lunicorn-lab.de/apt` by the GitHub Pages of this repository,
deployed from Actions: no package binary is ever committed, and the website's FTPS sync never touches it. The
domain is a CNAME to `64x-lunicorn.github.io` at Netcup, so the repository can move without users changing anything.

After every stable release, CI Release calls `publish-packages.yml` (never after a beta, which would reach every
apt user). The repository is rebuilt completely on every run, so there is no state to corrupt and a re-run gives the
same repository:

1. `release-apt.py select` takes the last three stable, published releases from the release list that carry a noble
   `.deb` and a checksum file, ordered by version.
2. `release-apt.py build` refuses a `.deb` whose SHA-256 differs from the one its release's checksum file lists, so
   the served files are the release assets byte for byte and their attestations still apply. It lays out
   `apt/pool`, `apt/dists/noble` (`Packages` from `apt-ftparchive`, and a `Release` dated by the newest release, not
   by the build), the public key `logsquirl-packages.asc` and the deb822 file `logsquirl.sources`.
3. `sign` writes `InRelease` and `Release.gpg`; `verify` checks both with the served public key alone, and a real
   `apt-get update` against the site (with `Signed-By` and a `file:` URI) must show no warning, offer every
   selected release and download the newest `.deb` unchanged.
4. The site is the Pages artifact; the `deploy` job deploys it.

Signing and deploying are separate jobs because a job has one environment: `build` runs in `release` and holds the
signing key `PACKAGES_GPG_PRIVATE_KEY` with `contents: read` only; `deploy` runs in `github-pages` with only
`pages: write` and `id-token: write`. The workflow fails when the secret holds another key than the pinned
fingerprint `51ABA6432D0407ED62E8EC403169E5DF85C9A3A3` (#379).

A failing run leaves the release out and the repository as it was: fix the cause and re-run the job, or dispatch
Publish Packages *from a release tag* (*Use workflow from*, or `gh workflow run publish-packages.yml --ref <tag>`);
any `v*` tag that contains the workflow works, the content is always the latest stable releases. A branch fails at
once with a message, because the signing key is only reachable from `v*` tags. Dispatch it for the first fill and
after a key rotation. A tag from before this workflow existed has no workflow to dispatch.

Rotating the key is a deliberate step, because every user then has to fetch the new key: create the key as #379
describes (without a passphrase: the secret is the key alone, and signing fails on a protected one), replace the secret, change `PACKAGES_KEY_FINGERPRINT` in `publish-packages.yml` and dispatch Publish
Packages from a release tag. The site is always built as a whole: the APT and the DNF repositories are one Pages
artifact.

**DNF repositories (#381).** The same run builds `dnf/fedora` (the Fedora 44 RPM) and `dnf/el10` (the Oracle Linux 10
RPM) from the last three stable releases that carry them, with `release-dnf.py` (`select`, `build`, `sign`, `verify`,
the same steps as for APT), and serves `logsquirl-fedora.repo` and `logsquirl-el10.repo` next to the key. Only the
metadata is signed, with the key of the APT repository (`repomd.xml.asc`; `repo_gpgcheck=1`, `gpgcheck=0`): signing an
RPM rewrites it, and it would no longer be the release asset the attestations cover. The signed `repomd.xml` carries
every package's checksum, so dnf still verifies each package. `build` refuses an RPM whose SHA-256 differs from its
release's checksum file. The Fedora RPM is built against Fedora 44; when a newer Fedora changes its Qt it may stop
working there until the build matrix follows, which is why the install instructions name the supported releases.
The workflow's last check runs dnf itself in clean `fedora:44` and `oraclelinux:10` containers
(`.github/scripts/check-dnf-repo.sh`): no warning from the signed metadata, every release offered, the downloaded RPM
is the release asset, and an older release upgrades to the newest with `dnf upgrade`. `createrepo_c` is given the
newest release's publication as revision, so a re-run is meant to give the same metadata.

#### Secrets and environments

The signing and upload secrets are not repository secrets but secrets of GitHub
Environments, so only the jobs bound to an environment can read them, and only
for the refs its deployment policy admits:

| Environment | Deployment policy | Secrets | Jobs |
|-------------|-------------------|---------|------|
| `release` | tags `v*` | `MACOS_P12_FILE`, `MACOS_P12_PASSWORD`, `APPLE_ID`, `APPLE_PASSWORD`, `APPLE_TEAM_ID`, `SENTRY_TOKEN`, `HOMEBREW_TAP_DEPLOY_KEY`, `PACKAGES_GPG_PRIVATE_KEY` | CI Release `sign-mac`, `sentry`, `update-homebrew` (the tap's deploy key, write access to `homebrew-tap` only); Publish Packages `build` (the package repository's signing key) |
| `github-pages` | branch `master`, tags `v*` | none | Publish Packages `deploy` |
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
| `deploy-website.yml` | dispatch only: by CI Release after a release is published, or by hand from the Actions tab | Build the website without the pages of unpublished releases and upload it |
| `ci-release.yml` | tag push `v*` | Sign and publish the CI Build packages of the tagged commit as a GitHub Release |
| `publish-packages.yml` | called by CI Release after a stable release; dispatch from a release tag | Build the signed APT and DNF repositories from the last three stable releases and deploy them with GitHub Pages |
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
  `3rdparty/CMakeLists.txt`, every tool version pinned in workflows, composite actions, the build images, the
  packaging scripts and `cmake/*.cmake` (Qt, OpenSSL, Boost, Ninja, CMake, Ragel, sccache, grype, NSIS, create-dmg,
  sentry-cli, linuxdeploy, minidump-stackwalk, the aqtinstall commit of `install-qt-action` and the Renovate config
  validator itself), the digests of CI Build's install-check images (`check_container`), and the hash-locked pip
  requirements.

**pip requirements.** Every `pip install` in CI and the build images reads a requirements file with exact versions and
hashes and passes `--require-hashes`: `docker/shared/aqtinstall-requirements.txt` (aqtinstall, installed into a
throwaway directory that is deleted once Qt is in the image), `.github/requirements/clang-format.txt`,
`.github/requirements/clang-tidy.txt`, `.github/requirements/e2e.txt` and `scripts/sbom/requirements.txt`. Each is generated from the `.in` file next to it by
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
into a composite action (`.github/actions/*/action.yml`), a Dockerfile, a script or a CMake module (`set(NAME "v")`
lines, as in `cmake/MinidumpStackwalk.cmake`), never into a workflow file: the
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
