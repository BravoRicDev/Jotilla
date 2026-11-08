# Note colors

Right-click the text area to open the wheel. Each original color fills one wedge;
the hovered or selected wedge gets a smooth contrasting outline. Click to apply and close.
Left/Right, or the arrows below the circle, browse palettes. Up/Down or Tab move
between wedges; Enter selects and Escape cancels. The palette name stays below
the circle. Browsing never changes the note.

Choosing a background also sets the palette for new notes this session. Colors
are drawn in shuffled cycles, using each distinct color once before reshuffling.
The star below the circle saves the displayed palette for future starts; click
it again to clear the favorite. **F** does the same. Starring does not recolor
the current note. The preference belongs to the current archive.

The title-bar menu still offers only **Pastel**, **Black and white** and
**Yellow only**, with their original colors and sequence. Unless a wheel color
or startup favorite is chosen, those defaults behave exactly as before.

With text selected, right-click opens **Selected text** mode: the chosen color
formats only that range and does not change the note or session palette. Without
a selection, **Note color** mode changes the background. Manual text colors are
preserved by background changes, undo/redo and reopening; once used, foreground
colors are under your control instead of following automatic black/white ink.

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
checked independently. For notes without manual text colors, black or white ink maintains at least
[4.5:1 contrast](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html)
on both the note and its darker title bar at full opacity. User-selected transparency
depends on the desktop behind the note and cannot carry that guarantee.

## Implementation

`src/colors.c` is the shared, immutable offline catalog. Persisted palette and color
indices are append-only; the first three palette IDs retain their old meaning.
Foreground choices are precomputed. Notes and archive previews use the same colors.

The native layered popup uses a premultiplied alpha surface. A small C rasterizer
caches analytic edge coverage for five- and six-wedge circles at the owner's DPI.
Each animation frame reuses the same surface and masks, with no allocations,
font creation or geometry work. Transitions last 120 ms, respect the Windows
animation preference and remove their timer when settled. Closing releases all
popup resources; no worker runs in the background. Hit testing is integer-only.

Color-only edits preserve emphasis and never rewrite the plain-text file. Manual
foregrounds use Rich Edit's native undo and RTF persistence; a per-note metadata
flag prevents theme normalization from overwriting them. Integration tests cover
contrast, rendering at 100–200% DPI, every wedge, navigation, cancellation, text
selection, session/favorite behavior, persistence, resource cleanup and upgrades.
