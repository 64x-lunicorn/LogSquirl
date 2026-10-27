# A context menu item is called from the event loop with what was selected as the menu opened

A plugin adds items to the context menu of the Log Lines (#859): that of the Text View, the Table View and the Filtered View. Triage settled that the callback gets a host-owned context struct with a leading `size` (ADR 0020), that the items go in a section of their own at the end of the menu, that they are always shown, with no prepare callback, and that registration follows the id pattern of ADR 0022. Left open were when the callback runs, what it is handed, and how the views of every tab of every window learn the items.

## Decision

- **What was selected as the menu opened.** The view takes its selection when it builds the menu, the same selection the menu's own entries act on: a right-click on a Log Line outside the selection has selected that Log Line first. The plugin is handed the Log File by the path its tab was added under, which view the menu was opened in, the numbers of the selected Log Lines from 1 in the order of the Log File (the Log File's numbers in the Filtered View too, and in the Table View under a Sort Order), and the text selected within one Log Line or cell. The list holds at most `LOGSQUIRL_SELECTED_LOG_LINES_MAX_LINES` numbers, the first ones; `selected_line_count` says how many were selected. The text of the Log Lines is not handed over: the plugin reads what it needs with `get_log_lines`.
- **From the event loop, once the menu has closed.** A chosen item posts its call to the view it was opened in. Qt drops the call when the view is deleted first, and the call asks again for the items when it runs: an item removed meanwhile, or of a plugin unloaded meanwhile, is not called, and neither is one whose tab is closing, which the window deletes from the event loop. The host's record of the item is marked removed by `unregister_context_menu_action`, as a menu action's is, so a call that passes the views' check is still dropped once the removal has returned.
- **The Plugin UI Port keeps the items; the views ask for them.** The Application Plugins' port holds the items of every plugin in the order they were added, and hands them to no window. Every tab hands its views one source, which asks the port as a menu opens; a tab added later, a Filtered View of a Search made later and a second window need nothing more. Unloading a plugin takes its items out of the port with its other contributions.
- **The ids come from the plugin's one counter** of ADR 0022: a context menu item's id never names a menu action of the same plugin, and `unregister_menu_action` does nothing for it, nor `unregister_context_menu_action` for a menu action's.
- **Labels may repeat.** Each call adds an item of its own: the port names an item by the host's record, not by its label.

## Considered Options

- **Calling the plugin while the menu delivers the choice.** Simpler, but the plugin then runs inside the menu's event loop, under the mouse event of the view, and a plugin that opens a dialog or closes the tab pulls the view from under that event. Posting the call costs one turn of the event loop.
- **Reading the selection when the call runs.** The selection the user saw when choosing is the one meant; another entry, a QuickFind or a reload may have changed it by then.
- **Handing the text of the selected Log Lines in the context.** A selection of a million Log Lines would be read for an item that may need one; the plugin reads the text it needs with `get_log_lines`, on any thread.
- **A flag saying the list was cut short.** The count of selected Log Lines says that, and more.
- **Forwarding the items to every window's port, as menu actions are.** Each window would build a copy of every item for menus it builds afresh each time anyway; asking one list as a menu opens needs no copy and no removal per window.

## Consequences

- A plugin cannot hide or disable an item for a selection it does not handle; it ignores the choice, or tells the user why with `show_notification`. A prepare callback can be added later as another function.
- A context menu with plugin items copies at most 1000 Log Line numbers when it opens, the first in the order of the Log File; one without plugin items copies nothing.
- `LogSquirlContextMenuContext` grows behind its `size`; `LOGSQUIRL_CONTEXT_MENU_CONTEXT_HAS` covers a member added later.
