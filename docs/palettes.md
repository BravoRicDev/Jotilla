# Note colors

Right-click the text area to open the wheel. Each original color fills one wedge;
the hovered or selected wedge gets a contrasting outline. Click to apply and close.
Left/Right, or the arrows below the circle, browse palettes. Up/Down or Tab move
between wedges; Enter selects and Escape cancels. The palette name stays below
the circle. Browsing never changes the note.

The title-bar menu keeps **Pastel**, **Black and white** and **Yellow only** for
new notes. Their colors and sequence are unchanged. The wheel changes only the
current note and stores that choice with its archive entry.

## Selected palettes

These complete five-color palettes come from [Color-Hex's popular collection](https://www.color-hex.com/color-palettes/popular.php).
Their original colors and order are preserved. The shorter wheel labels fit its footer.

| Wheel label | Original palette | Contributor |
|---|---|---|
| Pastel rainbow | [pastel colors of the rainbow](https://www.color-hex.com/color-palette/5361) | allyasdf |
| Faded Rose | [Faded Rose](https://www.color-hex.com/color-palette/2115) | dainty |
| Muted colors | [muted colors](https://www.color-hex.com/color-palette/240) | lm030191 |
| Soft Black | [Soft Black](https://www.color-hex.com/color-palette/1405) | jesshwv |
| Grey-Lavender | [Grey-Lavender Colors](https://www.color-hex.com/color-palette/425) | treasurina |
| Pastels | [Pastels](https://www.color-hex.com/color-palette/1223) | kayyim |
| Kawaii pastel | [kawaii pastel](https://www.color-hex.com/color-palette/9747) | aphrohdite |

Selection favors soft pastels, muted colors and neutral dark backgrounds, excluding
neon and highly saturated combinations. Comfort is a design judgment; contrast is
checked independently. Black or white ink is selected per color to maintain at least
[4.5:1 contrast](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)
on both the note and its darker title bar at full opacity. User-selected transparency
depends on the desktop behind the note and cannot carry that guarantee.

## Implementation

`src/colors.c` is the shared, immutable offline catalog. Persisted palette and color
indices are append-only; the first three palette IDs retain their old meaning.
Foreground choices are precomputed. Notes and archive previews use the same colors.

The native GDI popup allocates its state and drawing resources only while open.
Integer hit testing handles five- and six-wedge circles without timers or workers.
Changing ink preserves rich text, selection and undo history; color-only edits never
rewrite the plain-text file. Integration tests cover contrast, every wedge, navigation,
cancellation, persistence, resource cleanup and legacy archive adoption.
