# QuickFind searches a copy of the displayed lines, in Log Line numbers

QuickFind runs its search on a worker thread, while the UI thread keeps changing what the Filtered View displays: every Search progress tick unions new Matches in, every Mark change rebuilds the union of Marks and Matches, and the Search Session rebuilds its Context Lines. The worker used to read those sets directly, and the result was a data race that crashed inside the Roaring bitmaps. So when a QuickFind starts, including each restart of an incremental search, the UI thread copies the lines the view displays, and the worker searches only that copy. It reads their text from the Log File, whose reads are already safe off the UI thread, and never reads `LogFilteredData`. The copy holds Log Line numbers, so everything QuickFind keeps (its last and first match, the incremental search's start and initial selection) and the result it returns are Log Lines as well. The view converts its selection to Log Lines when a QuickFind starts, and converts the result back when it arrives.

## Considered Options

- **A lock around the displayed lines**, held by the worker while it searches. Rejected: a QuickFind over a large Filtered View holds it for seconds, and painting, Mark toggles and Search progress on the UI thread would wait for it. Holding it per line instead leaves the line numbers moving under the search, so a result could still land on another Log Line.
- **Reading the displayed lines on the UI thread**, with the worker asking for each line. Rejected: every line of the search costs a round trip through the event loop, which is slower than the search itself, and positions still move between two lines.

## Consequences

- Copying the lines costs about as much as their compressed Roaring bitmap: runs and dense stretches of Matches are stored as containers of at most 8 KiB each, so even millions of displayed lines take a copy of a few megabytes, once per QuickFind. The main view copies nothing: it searches every Log Line of the file.
- A QuickFind does not see changes made while it runs: new Matches from a Search in progress, new Marks, or changed Context Lines.
- A result can arrive on a Log Line the view no longer displays (a Mark removed, a Search cleared, Context Lines hidden). QuickFind then goes on from that Log Line in the same direction over a fresh copy, so it never selects a line that isn't a displayed match.
- A Mark added above the match while QuickFind runs doesn't move the result: the result is a Log Line, not a position in the view.
