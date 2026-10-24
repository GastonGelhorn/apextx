# NB4 design system

Every NB4 view is built from the same few tokens and parts. The tokens live in
`radio/src/gui/colorlcd/mainview/nb4_ui.h` (`Nb4Ui::`), and
`radio/src/tests/nb4_design_checks.h` checks every route against them, in both
orientations and every interface language.

## Tokens

| Token | Value | Use |
|---|---|---|
| `SpaceXS` | 4 px | Gap between a bar and its contents, inside a row |
| `SpaceS` (`Margin`, `Gap`) | 8 px | Page margin, gap between cards and rows |
| `SpaceM` | 12 px | Inset of content inside a card |
| `SpaceL` | 16 px | Gap between groups |
| `CardRadius` | 12 px | Cards |
| `ControlRadius` | 8 px | Buttons, wells, chips |
| `Touch` | 44 px | Smallest finger target, both directions |
| `IconTile` | 30 px | Icons on tiles and in headers |
| `IconInline` | 24 px | Icons next to text |
| `CaptionH` | 17 px | Height of a caption line |
| `ChipH` | 20 px | Height of a chip |

Type scale, smallest to largest: `TypeCaption` (XXS, captions only),
`TypeSmall` (XS), `TypeBody` (STD), `TypeTitle` (BOLD), `TypeValue` (L),
`TypeHero` (XL). The gauge digits keep their own fonts.

## Parts

- **Card** (`Nb4Ui::card`): raised surface for one group of content. One solid
  fill, a one-pixel border, `CardRadius`.
- **Well** (`Nb4Ui::well`): inset surface inside a card, such as a trim scale or
  a striped table row. `ControlRadius`.
- **Chip** (`Nb4Ui::chip`): outlined pill for a short status or value, no fill.
- **Caption** (`Nb4Ui::caption`): small upper-case label above or beside a
  value. The box is never shorter than the caption's line.
- **Paragraph** (`Nb4Ui::paragraph`): top-aligned wrapping text. A one-line
  label (`nb4Label`) is always centred vertically in its box.
- **Action** (`Nb4Ui::action`): a button at least `Touch` tall.
- **Header** (`Nb4Ui::header`): title, help and Back on a `Touch`-tall bar; a
  long title takes two lines rather than losing its end.

Page bodies are flex columns with `Gap` between rows; side-by-side buttons share
a row equally. The home instruments keep their own geometry, but their cards,
margins and gaps use the tokens.

## Icons

Each settings destination has its own glyph. Glyphs are 30 x 30 masks drawn at
the same optical size: line icons whose ink fills about 22 to 30 px of the
slot, never a solid square. An icon sits in the middle of its slot; on the
pointed header plate that is the middle of the plate's ink, not of its box.

## Checks

`Nb4Ux.EveryAvailableRouteActuallyOpensSomethingAndComesBack` runs the checks on
every route. They report:

- **overlap**: two pieces of text or controls drawn over each other;
- **bounds**: something sticking out of its parent or the screen;
- **clip**: text cut short, with dots or by its box;
- **touch**: a control whose touch area is under `Touch`;
- **icon**: an icon larger than its slot or off its centre;
- **grid**: text or a control inside a card that does not start on the spacing
  scale, or a flex gap that is not a token.

Set `NB4_DESIGN_REPORT=/path/report.tsv` to write every finding to a file.
EdgeTX's own list editors (inputs, mixes, outputs, logic, voice assignments)
are exempt from the touch rule until their rows are redesigned.

To review every view by eye:

```sh
NB4_SCREENSHOT_DIR=/tmp/shots build/nb4-device/native/gtests-radio --gtest_filter='Nb4*'
tools/nb4-contact-sheet.py /tmp/shots -o /tmp/sheets --match 'route-*'
tools/nb4-contact-sheet.py /tmp/before /tmp/after -o /tmp/compare
```
