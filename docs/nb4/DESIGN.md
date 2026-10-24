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
| `HeaderH` | 48 px | Shared page header, with 44 px actions |
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
- **Header** (`Nb4Ui::header`): title, help and Back on a `HeaderH`-tall bar; a
  long title takes two lines rather than losing its end.
- **Help** (`Nb4Ui::help`): a bare question mark without a border or fill,
  centred in a 44 x 44 px touch target.

Page bodies are flex columns with `Gap` between rows; side-by-side buttons share
a row equally. The home instruments keep their own geometry, but their cards,
margins and gaps use the tokens.

Settings, assignments, templates and contextual help use the complete viewport.
Their header and Back action stay in place while the body scrolls vertically.
They hide the cached navigation and Home layers below them; Back restores the
same cached menu. Confirmations and control-learning prompts remain transient
dialogs. Full page backgrounds are opaque.

Monitor tabs have separate 44 x 44 px targets. Channel banks show their actual
range (`1-8` on the original NB4), so identical glyphs do not hide which bank
will open. Selecting a tab scrolls its entire target into view.

## Icons

Each settings destination has its own glyph. Glyphs are 30 x 30 masks drawn at
the same optical size: line icons whose ink fills about 22 to 30 px of the
slot, never a solid square. Header glyphs sit in a fixed 44 x 48 px slot,
without a decorative plate. Changing a route glyph centres it again using
its actual dimensions. The same centring rule applies to menu tiles.

Virtual input labels use readable identifiers such as `I01:ST`, `I02:TH`
and `I03` instead of a small font pictogram. The input number remains visible
when a custom name matches a physical control. Lists, editors, source choices
and widgets use the same display formatter. Persisted source identifiers keep
their original format for model and YAML compatibility.

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
Native list editors (inputs, mixes, outputs, logic, voice assignments and
variables) use rows at least 44 px high. The route checks include populated
logic and voice rows, without a touch-size exemption.
Touch checks also include buttons operated only by the touch screen, such as
tab selectors and dialog header actions, even when they are outside the key
focus group. Navigation tests verify full page coverage, Back to the cached
menu and the visible area of every selected monitor tab.

To review every view by eye:

```sh
NB4_SCREENSHOT_DIR=/tmp/shots build/nb4-device/native/gtests-radio --gtest_filter='Nb4*'
tools/nb4-contact-sheet.py /tmp/shots -o /tmp/sheets --match 'route-*'
tools/nb4-contact-sheet.py /tmp/before /tmp/after -o /tmp/compare
```
