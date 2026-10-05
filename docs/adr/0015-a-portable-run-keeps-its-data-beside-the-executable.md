# A portable run keeps its data beside the executable

The portable package kept its settings in `logsquirl.conf` beside the executable and, in a build with crash reporting, its crash dumps there too. Everything else it stored went through `QStandardPaths::AppDataLocation` and `AppConfigLocation`, which are named after the executable, so the portable build wrote its Log Formats, plugins, plugin configuration, Team Folder clone and theme stylesheets into `%APPDATA%\logsquirl_portable\` and `%LOCALAPPDATA%\logsquirl_portable\` (#602). Taken to another machine, it arrived without them. Each place decided for itself: `PersistentInfo` whether the run is portable, the crash handler by the build alone, and the rest not at all.

## Decision

- `DataLocation` (library `logsquirl_datalocation`, Qt Core only, below every library that stores something) decides once per run whether the run is portable: when its build forces it, or when it finds `logsquirl.conf` beside its executable. Every executable defines `DataLocation::ForcePortable`, as it defined `PersistentInfo::ForcePortable` before: true for the portable package, the command line tool and the test binaries.
- A portable run's data directory and configuration directory are both the executable's directory. An installed run's are `AppDataLocation` and `AppConfigLocation`, unchanged.
- Everything that stores something asks `DataLocation`: the settings (`logsquirl.conf`, `logsquirl_session.conf`), the Log Formats (`formats`), the user plugin directory (`plugins`), the plugin configuration (`plugin_config/<id>`), the Team Folder clone (`teamfolder`) and the record of its last sync beside it (`teamfolder-sync.ini`, #714), the theme stylesheets (`themes`) and the crash dumps (`logsquirl_dump`).
- The index cache stays in `CacheLocation`: it is a cache, rebuilt when missing, and does not belong in a folder that is copied from machine to machine.

## Considered Options

- **A `data` folder beside the executable** holding everything but the settings. It would keep the user's plugins apart from the shipped ones, but the portable package would then have two plugin folders side by side, and the one the user sees first, `plugins`, would not be the one *Install* writes into.
- **Deciding in the settings library.** `PersistentInfo` already decided portability, but the Plugin Catalog and the Log Format Catalog must not link the settings store (#236), so the decision moved below them.

## Consequences

- On Windows and Linux a portable run's user plugin directory is its application plugin directory. The Plugin Catalog scans that folder once. This departs from ADR 0014 for the portable run only: the application plugin directory is written to there, and an update from the catalog replaces the shipped copy instead of shadowing it, so going back to the shipped version means unpacking the package again. ADR 0014 still holds for every installed run.
- A portable package updated in place would no longer see what the earlier one kept in `%APPDATA%\logsquirl_portable\` and `%LOCALAPPDATA%\logsquirl_portable\`. So a portable run takes that data over on its first start (#613): when none of `formats`, `plugin_config`, `teamfolder` and `themes` beside its executable holds anything, it copies those folders and the plugins from its `AppDataLocation` and `AppConfigLocation` beside the executable, keeping a plugin folder the package ships, and writes `logsquirl_taken_over.txt` there so it never does so again. It does not look for `teamfolder-sync.ini`: no version that kept its data in those folders wrote one, so a Team Folder taken over shows *never* as its last sync until it syncs. The old folders stay as they are; a failure is logged and the run starts. An installed run takes nothing over.
- A portable run needs a folder it may write to; unpacked under `Program Files` it can neither save its settings nor install a plugin, as before.
- An E2E instance that is made portable (the macOS clone in `tests/e2e/isolated_instance.py`) keeps its plugins beside its executable, and the fixture looks for them there.
