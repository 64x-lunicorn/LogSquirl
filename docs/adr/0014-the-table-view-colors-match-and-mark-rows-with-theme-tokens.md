# The Table View colors the Rows of Matches and Marks with Theme Tokens

The Text View shows whether a Log Line is a Match, a Mark or both as a bullet in its gutter, and the overview beside the scroll bar as a line. The Table View has no gutter, so the Line Decorator colors the whole Row instead (`LineStatusDisplay::AsBackground`). It used the gutter's colors for that: `LineStatusColors`, pure red, dodger blue and violet, the same in every Theme. A bullet in those colors is a small mark on the Theme's background; a Row in them is a surface the Row's text sits on. With a Search of `warn|error` most visible Rows turned solid red, light text on it in Smyck, dark text in Light, and a Highlighter's colors drowned in it (#590).

So the Row colors are three Tokens of every Theme: `MatchRow`, `MarkRow` and `MarkedMatchRow`. Each built-in Theme gives them the gutter's hues as tints of its own Base: pale in a light Theme, dark in a dark one, deep and saturated in High Contrast. The Theme's text reaches 4.5:1 on each (7:1 in High Contrast), and the three differ from each other, from the Base and from the selection. `theme_test` checks this for every built-in Theme.

This amends ADR-0006, which made a Color Label the only thing in a Log Line a Theme colors. The Row colors are Tokens and not a palette like the Color Labels': no user colors them, so they need no rule about whose colors a slot holds, and a Theme sets them like any other Token. Highlighters and Highlighter Sets stay the user's alone, and a whole-line Highlighter and the selection still win over the Row color.

The Table View's delegate reads the Tokens from `Theme::active()` when it paints and hands them to the Line Decorator in its `LinePalette`, beside the colors it takes from the Qt palette. Nothing caches them, so a Theme switch recolors the Rows with the repaint the switch causes (ADR-0004). A `LinePalette` without Row colors, as the Text View builds it, falls back to `LineStatusColors`.

## Considered Options

- **Deriving the Row colors from `LineStatusColors` and the Base** (a fixed blend, e.g. 20 % over the Base). No new Tokens, but a blend that reads well over white is muddy over Smyck's near-black, and a Theme could not choose its own hues, as Smyck does with its scheme's red, blue and magenta.
- **Using the new Tokens for the gutter bullets and the overview too.** One set of colors for all three places, but a subdued tint that text reads on is too faint for a bullet or an overview line to stand out against the Base. The bullets and the overview keep `LineStatusColors`, whose meaning does not change.

## Consequences

- Whoever adds a Theme adds three Row colors, and `theme_test` holds them to the text contrast.
- A Dark override stored before these Tokens existed does not name them; Dark's own Row colors apply. They do not follow an overridden Base or Text.
- The Text View and the Table View show a Match in different colors: a red bullet beside the Log Line, and a pale or dark red Row.
