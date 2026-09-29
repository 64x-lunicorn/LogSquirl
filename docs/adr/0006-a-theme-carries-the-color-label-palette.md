# A Theme carries the colors of the Color Labels

Until the Smyck Theme (#353) a Theme colored the application around the Log Lines and nothing in them: "A Theme does not color Log Lines — that is the Highlighter Set's job." The nine Color Labels had one set of colors, picked for a light window, and they kept it under every Theme. That rule no longer holds for them. Smyck is a terminal color scheme, and half of what makes it one are the colors a labelled word is painted in; the same pastel labels under a light Theme and under Smyck look like two products.

So a Theme carries the colors of the nine Color Labels beside its Tokens, and applying a Theme gives them to the Color Labels. Light, Dark and High Contrast carry the colors LogSquirl has always given them, so nothing changes for anyone who does not choose Smyck. Highlighters and Highlighter Sets stay the user's alone: the Theme does not touch them, and a Color Label is the only thing in a Log Line a Theme colors.

A Color Label the user colored is not touched. A slot follows the Theme only while its colors are those some built-in Theme gives it; the first color the user picks takes that slot out of the Theme's hands for good, under every Theme. A Theme that gives a Color Label no text color of its own leaves an invalid color there, which the settings store used to write and read back as opaque black; it now writes an empty string for it, and a Color Label saved by an earlier version still counts as the Theme's.

The wiring follows ADR-0004: `HighlighterSetCollection::followTheme()` registers a refresh with `Theme::whenApplied()`, which writes the new colors and saves them. `main()` registers it once, before the first `Theme::apply()` and before any window exists — refreshes run in the order they were registered, so the Color Labels are in place before the views that paint with them refresh. Most switches are between Themes that give the Color Labels the same colors, so the refresh first asks the held copy whether anything would change at all; only a switch that really recolors one syncs the settings store and writes to it.

The Text View was handed the colors together with the words and holds them in its Decoration Setup, so it pushes them in again from its own refresh. The Table View's delegate reads the colors when it paints, so the Table View's refresh only repaints (#561). The Highlighters Dialog edits a copy of the collection, and that copy follows the Theme too, or pressing OK after a switch would put the colors of the Theme before it back.

## Considered Options

- **A preset in the Highlighters Dialog** ("load the Smyck colors"). It keeps the layers apart and needs no rule about whose colors a slot holds, but it leaves the labels looking foreign next to the Theme until the user finds the button, and every Theme switch back and forth is manual work.
- **Asking once on the switch.** A dialog on a Theme switch, remembered in the settings. Same result as this decision in the common case, at the price of a modal question in the middle of a preference change and one more stored flag.
- **Replacing every Color Label, the user's own included.** Simpler to implement and to explain, but it throws away work: the colors a user picks for a label are a choice about their Log Files, not about the look of the window.

## Consequences

- A Theme is no longer only a set of Tokens: whoever adds one adds nine Color Label colors too, and their text reaches 4.5:1 on their background like every other pair of Tokens.
- Applying a Theme writes the settings when a Color Label changed, so such a Theme switch is a write, not only a repaint.
- A user who colors a Color Label to a built-in Theme's exact colors has, for the Theme, not colored it at all.
- A Theme also carries 16 basic ANSI colors (#573): whoever adds a Theme adds those too.

## Note (2026-09-28, #590): a Theme colors Match and Mark Rows too

Color Labels are no longer the only thing in a Log Line a Theme colors: in the Table View the Row of a Match, a Mark and a Mark that is a Match takes its color from the Theme's `MatchRow`, `MarkRow` and `MarkedMatchRow` Tokens (ADR-0013). Whoever adds a Theme adds these three too, with its Text reaching 4.5:1 on each.

## Note (2026-09-29, #573): a Theme colors ANSI colors too

Under the setting *ANSI color sequences* = *Show colors* the Text View paints a Log Line in the colors its ANSI color sequences ask for, and the 16 basic ones (codes 30–37, 90–97, 40–47, 100–107 and indices 0–15) are the Theme's: `Theme::ansiColors()`, xterm's defaults for Light, Dark and High Contrast and the SMYCK scheme's for both Smyck Themes. Whoever adds a Theme adds these 16 too. A foreground that falls below 3:1 against what it is drawn on is moved toward the Theme's text color by the Decoration Setup, so a Theme's basic colors need not all read on its own background.
