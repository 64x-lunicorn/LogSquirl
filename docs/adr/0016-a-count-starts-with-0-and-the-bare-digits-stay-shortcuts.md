# A count starts with 0, and the bare digits stay shortcuts

The main view and the Filtered View take a count before a command, as *vi* and *less* do: the digits typed collect in the view and the next *Move selection up* or *Move selection down* moves that many lines, *Jump to line number* goes to the line of that number. The count dates from glogg (2010). The Crawler Widget later gave the bare digits shortcuts of their own: `1` to `3` the visibility of the Filtered View (klogg #608, 2023), `4` to `9` the search buttons (klogg #724, 2024). They are registered as `Qt::WidgetWithChildrenShortcut` on the Crawler Widget, so they fire whenever the focus is inside it, and Qt tries a shortcut before it delivers the key to the focused widget. A digit therefore never reached the view, and the count could not be typed with the default shortcuts in place (#600). The Search line is not affected: a line edit takes printable keys before any shortcut, so the digit shortcuts only ever fire from the views and the buttons around them.

## Decision

- A count starts with `0`: `05j` moves the selection five lines down, `012k` twelve up. `0` has no default shortcut, and its leading zero does not change the number.
- While a count is being typed, the view accepts the `ShortcutOverride` of every bare digit, so `1` to `9` go to the count, not to their shortcuts. `AbstractLogView::event()` does this.
- A count ends when a command takes it, or two seconds after its last digit. From then on the bare digits are shortcuts again.
- `0` still scrolls to the beginning of the line, as it did when no count was typed; it now also starts a count.
- A digit that has no shortcut, because the user removed or changed it, starts a count by itself, as before.

## Considered Options

- **The view takes every bare digit while it has the focus.** The count would work as in *vi*, but the view has the focus nearly always, so `1` to `9` would do nothing where they are used. The user guide documents them for the search buttons and the visibility (#594); they would be dead keys.
- **Holding the first digit back until the next key.** The view would keep a digit, count with it if a command followed, and fire the digit's shortcut after a timeout otherwise. Every visibility and search button change would come late, and the view would have to fire another widget's shortcuts itself.
- **Other default keys for the visibility and the search buttons.** It frees the digits, but changes keys users have learnt since 2023 and that the user guide names.
- **A count only with the keypad.** Qt matches a keypad digit to the shortcut of the same digit, and not every keyboard has a keypad.

## Consequences

- A count takes one key more than in *vi*: `05j`, not `5j`.
- Starting a count scrolls the view back to the beginning of the line.
- For two seconds after a count, or until its command, the digit shortcuts do not fire while the view has the focus.
- A shortcut the user gives to `0` takes `0` before the view: a count then cannot be started, unless one of `1` to `9` is free.
