# Presentations share signals and a plain interface, not a base class

The upper pane shows a Log File as one of two Presentations, the Text View or the Table View, and the widget coordinating a Log File's panes must treat them alike. The obvious shape is a common widget base class, but Qt forbids it: the Text View is a `QAbstractScrollArea` and the Table View a `QTableView`, and a class cannot inherit two `QObject`s. So a Presentation is two things. Towards the coordinator it emits the same set of signals the Text View already emits (a new selection, Marks, adding to or excluding from a Search, Search Limits, Color Labels, the scratchpad), and the coordinator connects each Presentation to the same slots. For what the coordinator asks or tells it (the selected text, the Log Line at a point, showing a Log Line, repainting, the font) a Presentation implements a plain C++ interface without `QObject`, and the coordinator holds the active Presentation through it. Everything a Presentation hands out is a Log Line, never a position in its own widget, so a Row is never mistaken for a Log Line.

## Considered Options

- **A `QObject` adapter wrapping each widget**, forwarding its signals and holding the interface. Rejected: a second object per Presentation whose only job is to forward, with nothing else using it.
- **Only a shared signal protocol, without a common type.** Rejected: the coordinator would keep a pointer to each widget and keep asking which one is active before every query.

## Consequences

- The signal set is a contract kept by convention: nothing in the compiler checks that the Table View emits every signal the Text View does. A new signal has to be added to both Presentations; the coordinator connects the set in one place, for the Presentations and the Filtered Views alike, so it is connected once (#391). A signal for something only the Text View does (turning following on or off from the view, zooming with the wheel, the exit-view shortcut) is not in the set: the Table View does not declare it, and the coordinator connects it for the Text View alone (#236).
- The Filtered View is drawn like the Text View but is not a Presentation; nothing in the interface prevents it from gaining a Table View later.
- What every view of a Log File shows alike (the Policies, the follow allowance, the Color Labels, the Search Limits) is told through the interface as well, by the Log File's View Set, which holds the Presentations through it and the Filtered Views as they are (#242). A Presentation that has no use for one of these (the Text View for the QuickFind Policy, the Table View for the follow allowance) ignores it, so the View Set never asks which Presentation it holds.
