# A plugin ships Filter Groups and Highlighter Sets in its manifest, and they are never stored

#860 decided that a plugin brings Predefined Filters and Highlighter Sets the way it brings Log Formats: declaratively, in its manifest (ADR 0021), with no host function. A Logcat plugin wants to offer a "Crashes" filter group and a set that colours log levels. The Log Formats a plugin names in `formats` go into the Log Format Catalog, which is rebuilt in memory and never written anywhere. Filter Groups and Highlighter Sets, though, live in collections the user edits and that are saved to the settings store as a whole, and so do the Team groups, which come from elsewhere and are kept out of the user's settings by hand (#694).

## Decision

- **Two optional manifest keys, `predefined_filters` and `highlighter_sets`**, each a list of paths relative to the plugin directory, with the path rules of `formats`: a path that leaves the plugin directory makes the manifest invalid. Each file is one the Predefined Filters or the Highlighters dialog exports, read by the Group Exchange; a file that cannot be read or holds groups of the other kind is skipped with a logged warning.
- **They are read for every enabled plugin, loaded or not, at the points the Log Format Catalog takes the plugins' Log Formats**: once the Application Plugins have discovered the plugins, and whenever Plugin Management says a plugin was enabled or disabled. Each window reads them and hands them to the Predefined Filters Collection and the Highlighter Set Collection; the first window changes the collections, the others find them unchanged, as with the Team groups.
- **The collections hold them apart from the user's own groups**, as they hold the Team groups: in a list of their own, each group with its Source, the plugin's name. `saveToStorage` writes only the user's own, so no save path, the dialogs' OK and Apply, the Highlighters menu, a Theme switch, a Team sync, an import, can write one. The dialogs show them in a read-only section with *Copy to my groups*; the Filters panel and the Highlighters menu list them after the user's own and before the Team groups, named "<name> (<plugin name>)".
- **A plugin's group goes by an id of its own**, `plugin:<plugin id>:<id in the file>`, so it is never the id of one of the user's groups, of a Team group, or of another plugin's. An import into the user's groups reserves these ids, as it does the Team groups', so an imported copy gets a fresh one. A second group of the same id in one plugin is skipped.
- **Whether a plugin's Highlighter Set is active is the user's**, stored with the active sets under `active_plugin_sets`, as `active_team_sets` stores the Team sets'. Until the plugins are discovered the activation is kept; once they are, the activation of a set that is not there is dropped. So disabling the plugin switches its set off, and when no other set is on, the Log Lines show as with no set chosen; enabling it again brings the set back switched off.

## Considered Options

- **Merging the plugin's groups into the user's list, marked read-only.** One list is simpler to show, but every save path writes the whole list, and each would have to filter them out again; one missed path writes a plugin's group into the user's settings for good.
- **A host function to register groups at run time.** It needs the library loaded, so a disabled or not yet loaded plugin ships nothing, and it is out of scope for #865.
- **Keeping the id the file holds.** A plugin's file exported from the author's own set carries the author's id: a user who imported the same file, or two plugins shipping one file, would have two groups of one id, activated and pinned as one.
- **Not keeping the activation of a plugin's set across a restart.** Nothing of the plugin would be stored, but a user would have to switch the set on again at every start.

## Consequences

- A plugin's group shows only once the plugins are discovered, shortly after the first window is on screen; a Filters panel pin of a plugin's filter made before is not restored, as the panel drops the pins of filters it does not list when it first fills.
- A file a plugin ships cannot be told apart from a user's export, and need not be: an author builds the groups in LogSquirl and exports them.
- Plugin Management shows no warning for a group file that cannot be read; the log does. A later ticket may show it on the plugin's card, as the Log Format warnings are.
