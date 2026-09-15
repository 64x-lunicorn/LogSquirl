# Widgets refresh after the Theme is applied, not from Qt's change events

A Theme can be switched while windows are open. Qt updates the palette and the application stylesheet of every widget by itself, but some widgets derive further state from the Theme: icons chosen by `Theme::usesInverseIcons()`, and stylesheets built from Tokens or from palette roles that are resolved when the stylesheet is set. These have to be rebuilt on a switch. The obvious place is a `changeEvent` handler for `StyleChange` or `PaletteChange`, and that is where the archived switching attempt put it. It crashed: `QApplication::setStyleSheet()` repolishes every widget, the Filters Panel reacted to the resulting `PaletteChange` by giving its tree a widget style of its own, and the style sheet style then read that tree's style while still repolishing it (ASan: SEGV in `QStyleSheetStyle::styleHint`, called from `QWidget::setPalette` in `FiltersPanel::applyCurrentPalette`, inside `QStyleSheetStyle::repolish`). A handler that sets a stylesheet sends `StyleChange` itself, so these handlers also loop.

So `Theme::apply()` runs, after the palette and stylesheet are completely in place, every refresh a widget registered with `Theme::whenApplied( this, refresh )`. The widget is the context: its refresh stops when it is destroyed. Widgets no longer react to `StyleChange` or `PaletteChange` for Theme-derived state, and no widget sets a style or palette of its own to follow the Theme; they inherit the application's.

## Considered Options

- **`changeEvent` handlers, deferred to the event loop.** Rejected: the deferred call runs whenever the loop gets to it, and Qt sends these events for reasons other than a Theme switch too. Every switch queued one call per widget, captured by a raw pointer.
- **A `QObject` notifier with a `changed()` signal.** Equivalent, but it needs a moc'ed object in the settings library, which has none; a context-guarded list of callbacks does the same without one.

## Consequences

- A widget that derives anything from the Theme at construction registers its refresh, or it keeps the look of the Theme it was built under.
- The refreshes run synchronously at the end of `apply()`, so a refresh must not apply a Theme itself.
