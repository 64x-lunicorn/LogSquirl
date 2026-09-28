# A plugin from the catalog goes into the user plugin directory and wins over the shipped copy

The Plugin Catalog searches two plugin directories: the application plugin directory (`Contents/PlugIns` inside the macOS bundle, `plugins` next to the executable on Windows and Linux) and the user plugin directory (`plugins` in the application data location). Until #595, installing or updating a plugin from the catalog wrote it into whichever came first, which was the application plugin directory. On Windows that is under Program Files and not writable without admin rights; on macOS it is inside the signed bundle, whose signature a new file breaks. The dialog's *Plugin Folder* button opened the user plugin directory, so where a plugin was installed was not where the user was shown to look.

## Decision

- Installing and updating from the catalog always writes into the user plugin directory. `PluginCatalog::userPluginDirectory()` names it, and both the install and *Plugin Folder* use it.
- The application plugin directory holds the plugins shipped with LogSquirl and is only read.
- The user plugin directory is scanned first. Discovery keeps the first plugin of an id, so a plugin in the user plugin directory wins over the shipped plugin with the same id. An update of a shipped plugin therefore goes into the user plugin directory like any other install, and the new version is the one listed and loaded.

## Considered Options

- **Refusing to update a shipped plugin.** The dialog would say that a shipped plugin is updated with LogSquirl. Simple, but the catalog would offer an update the user cannot take, and a fix to a plugin would have to wait for the next LogSquirl release.
- **Keeping the higher version of the two.** Discovery would compare the manifests' versions instead of taking the first. A user who installs an older version on purpose would not get it, and the rule would be one more thing a plugin author has to know; the order of the directories is enough.

## Consequences

- A catalog install works without admin rights on every platform and never touches the signed macOS bundle.
- A plugin a user updated stays in use after a LogSquirl update that ships a newer copy, until the user updates it from the catalog again or deletes it from the user plugin directory. The user guide says how to go back to the shipped copy.
- A plugin copied by hand into the user plugin directory also wins over a shipped plugin of the same id.
