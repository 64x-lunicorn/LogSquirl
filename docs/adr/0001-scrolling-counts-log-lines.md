# Scrolling counts Log Lines, not Visual Lines

With text wrapping on, the vertical scrollbar counts whole Log Lines, and nothing keeps an index of how many Visual Lines each Log Line wraps into. Knowing that count means reading, decoding and wrapping each line. An index would therefore be work proportional to the Log File, and every width change would throw it away. That breaks the rule that layout work is bounded by the Viewport. What wrapping needs is handled by the Scroll Position instead, which is a Log Line plus the Visual Line within it that is shown first. The wheel, the keys and autoscroll move in Visual Lines and only wrap the lines they pass over, so every Visual Line can still be reached.

## Considered Options

- **Estimated Visual Lines**: a sparse estimate of Visual Line counts, refined in the background as regions get wrapped. Rejected: it is work over the whole file, a width change discards it, and the thumb drifts while the estimate settles.
- **Sub-steps per Log Line on the scrollbar**, so that dragging can stop partway through a Log Line. Rejected: files with more Log Lines than the scrollbar's integer range already need scaling, and sub-steps would push that limit much lower.

## Consequences

- The thumb is not proportional to Visual Lines. A stretch of long lines gets as much thumb travel as the same number of short lines.
- The thumb keeps its unwrapped size.
- Dragging lands on a Log Line's first Visual Line. The one exception is the scrollbar's maximum, which is the bottom Scroll Position: the last Visual Line of the Log File on the Viewport's last row. Finding it wraps backwards from the end of the Log File, at most one Viewport height of Visual Lines.
- While the view moves through a single very tall Log Line, the thumb does not move.
- In the Filtered View the scrollbar counts the Log Lines it displays, not the Log Lines of the file: a position on it is a place among the Displayed Lines. Everything above holds there over those positions.
