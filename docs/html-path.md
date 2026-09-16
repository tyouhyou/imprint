# Imprint HTML/CSS Subset (`.html`)

An **optional declarative front-end** that renders a narrow HTML/CSS subset
through the existing widget tree, complementing the `.ui` design file.
It lets authors with HTML/CSS habits describe screens without learning the
`.ui` grammar or the C++ builder API — the parser maps supported elements
onto the existing widget tag table, and the whole result materializes
through the shared `ui_node` design-file layer
(`docs/ARCHITECTURE.md` §4.10; shared design-file semantics in
`docs/design-file.md`).

No JS, no CSS cascade engine: this is a static declarative front-end onto
widgets, deliberately narrow. **The whitelist tables below are the
contract.** Anything not in them constructs nothing — there is no
blacklist to read, the table *is* the boundary.

## Pipeline

```
page.html ─► parse_html(text) ─► ui_node ─► build(host, root) ─► widget tree
                  (imui)         (shared IR)       (shared materializer)
```

`parse_html(const char*, bool *ok)` is the only parsing entry (init path,
may allocate; a parse happens once at document load, never on the frame
path). The produced `ui_node` is byte-identical in shape to what
`parse_ui_text` produces, so every downstream rule — the property table,
materialization tolerance, `find_by_id` wiring, event binding in C++ —
applies unchanged.

## Root and document structure rules

- `<html>` / `<head>` containers and their contents **never construct
  widgets**. `<style>` blocks are consumed for rules; everything else in
  the head (`<title>`, `<meta>`, `<link>`, `<script>`, ...) is ignored.
  There is no resource loading of any kind — no external CSS, fonts,
  images, or scripts.
- Only `<body>`, its text content, `<style>` rules, and the whitelisted
  elements construct.
- Root handling mirrors the `.ui` convention: when the body has exactly
  one top-level container child, that node is returned as the document
  root, so its container properties (`spacing`/`padding`/`wrap`) apply to
  the build host; otherwise a pseudo-root wraps the body's children and
  they materialize into the host one by one. Exception — a flex body is
  kept: when the body's folded style (inline `style=` plus exact-`body`
  stylesheet rules, below) carries `display: flex`, the body converts to
  a `row`/`column` node per the `div` mapping and is returned as the
  document root itself. The host then takes the body's container
  properties **and** box dress, so a page gradient, `padding`, and the
  viewport-centering wrapper (`justify-content: center` +
  `align-items: center`) ground the screen, while the content width
  (e.g. a `620px` chassis) constrains the child instead of being
  dropped. Plain bodies hoist exactly as before.
- Stylesheet rules match the body element like any other (tag `body`,
  plus its `id`/`class` attributes); `html`/`head` still match nothing
  and never build. The body joins every ancestor chain, so
  `body`-anchored descendant selectors match uniformly in kept and
  hoisted documents.
- Element tags and attribute names are lower-cased (HTML is
  case-insensitive); attribute values and `id` keep their case. CSS
  keyword values (`display` / `flex-direction` / `flex-wrap` / `auto` /
  units) and color names are ASCII case-insensitive; `id` selectors stay
  case-sensitive.
- Text content: the trimmed concatenation of an element's text children
  (whitespace-only text nodes are dropped). Bare text inside a container
  becomes an anonymous `label` (a container's text must go somewhere).
  Entities: `&amp;` `&lt;` `&gt;` `&quot;` `&#39;` `&nbsp;` — nothing else.
  `&nbsp;` folds to a plain space (no nowrap / no-collapse distinction
  until H-1 text wrapping exists).
- The whole path is **tolerant and never fails**: `ok=false` only for a
  document nothing can build (mirrors `parse_ui_text`). Violations are
  reported per the Tolerance table below.

## Page box

- The `<body>` `style` feeds the document-level page box (size and
  background): `width` / `height` accept `Npx` values > 0, independently
  per axis; `background-color` accepts the color forms. `%` has no parent
  box to resolve against and `auto` means "ask the shell", so both stay
  absent — as do non-positive/malformed values (silently, per the
  Tolerance table).
- The page travels *beside* the widget tree (`html_page`, filled by the
  three-argument `parse_html`): the consumer resolves the initial
  screen/buffer size from it *before* materializing. Sizing at creation
  never touches the "buffer never resizes" presentation contract.
- Resolution order, per axis: document page, then the shell/platform
  size, then the app default. The consumer warns whenever it falls back
  to the default, so a missing size is loud during bug-chasing.
- A fixed-size host buffer (NDS framebuffer, an externally supplied
  buffer) wins over the page; a conflicting page size is ignored with
  an LW warning.
- Multi-document hosts size the window from the first usable document;
  each screen resolves its own page against the window box.
- A single top-level container's own `width`/`background` still do not
  apply to the build host (only spacing/padding/wrap do) — the page box
  above is the only channel for document-level size/background.

## Lexical rules

- Attribute values come double-quoted (`id="x"`), single-quoted
  (`id='x'`), or unquoted (`id=x`). Inside quotes the other quote stays
  literal; an unquoted value ends at whitespace or `/` (so `value=30/>`
  is value `30`, self-closed). Entities decode in both quoted forms.
  Valueless attributes (`checked`) take their name as the value.
- `br` is void: `<br>`, `<br/>`, `<br />`, `<br></br>` are equivalent
  and it never takes a close; `</br>` is purely ignored. Every other
  whitelisted element needs its close; unclosed frames are finalized
  at EOF.
- `<!-- ... -->` and `<!DOCTYPE ...>` are consumed silently. A `<`
  that does not start a tag consumes through the next `>` (or EOF), so
  a bare `<` in text swallows up to the next tag end.
- Case: tags, attribute names, and CSS property names fold to
  lower-case; CSS keyword values and color names are ASCII
  case-insensitive; `id`s (and `#id` selectors) stay case-sensitive.
- Error recovery: a closing tag pops open elements up to the match
  (see Tolerance); text outside any element becomes top-level labels.

## Tolerance (how an off-whitelist construct is handled)

| Construct | Handling |
|---|---|
| Element not in the whitelist | **LW warning + skipped**; its content is dropped. "Not in the table = not built" — the honest signal, so mistyped customs (`<metter>`) or unsupported HTML (`<table>`, `<input>`, `<form>`) never render a wrong structure |
| `<br>` inside an inline element (`<span>`) | the parser warns (no line breaking until H-1) and degrades the break to a word space in the single-line label; the spacer child is dropped by leaf materialization with a warning (the same rule as `.ui` leaf children); place `br` as a child of a container |
| Attribute not in the whitelist | silently tolerated (`class=` drives selector matching) |
| Selector beyond tag/`.class`/`#id`/descendant/comma (child/sibling/attribute/pseudo) | silently inert, body consumed (pseudo-element selectors log one LW per rule — they usually carry visible content intent) |
| Unknown `var(--name)` without fallback | declaration dropped silently (malformed-value tolerance) |
| Style declaration not in the whitelist | **LW warning + ignored** (the element keeps its default presentation) |
| Malformed value (bad color, bad number, bad percent) | silently defaulted (the shared property table's tolerance) |
| Stray/mismatched closing tag | closes open ancestors up to the match (unbalanced intermediates are finalized along the way); ignored when no open frame matches. HTML5 would ignore the stray tag instead — tolerated deviation, silent |

## Whitelist — elements

| Element | `ui_node` tag | Notes |
|---|---|---|
| `div` | `column` / `row` | default `column` (block reading order); `flex-direction: row` → `row`, and a bare `display: flex` also selects `row` (the CSS flex default — stylesheets that lay out with `display: flex` alone depend on it). `display: block` (or anything else) keeps `column`. The div is a content-measuring flex container, **not** HTML block layout; it never stretches to fill a parent's main axis. `flex:` markup drives fill |
| `p`, `span`, `label`, `small` | `label` | single-line labels; no wrapping until H-1 (`small` defaults to `font-size: 12px` unless an explicit `font-size` wins) |
| `button` | `button` | `text` = element text content |
| `checkbox` | `checkbox` | `text` = content; `checked` = **attribute presence** (HTML semantics) |
| `radio` | `radio` | `text` = content; `checked` = presence; `group` |
| `br` | an empty label | blank-line spacer, height = one text line; meaningful as a child of a column. Void element: `<br>`, `<br/>`, `<br />` are equivalent and it never takes a close; a stray `</br>` is ignored |
| `toggle` | `toggle` | `checked` = presence |
| `gauge` | `gauge` | `min` / `max` / `value` |
| `knob` | `knob` | `min` / `max` / `step` / `value` |
| `trend` | `trend` | sized with `width` / `height` |
| `meter` | `progress_bar` | degraded stand-in (HTML meter is a horizontal scalar); `min` / `max` / `value` |
| `svg`, `vectordial` | `svg` | vector-dial subset (§SVG subset): `viewBox` + `line`/`text` children; `g` folds its presentation attributes onto them; `vectordial` is the same widget under an instrument name (the alias costs one table row, the implementation is shared) |

## Whitelist — attributes

| Attribute | Applies to | Meaning |
|---|---|---|
| `id` | any element | `find_by_id` handle (unquoted digits accepted, stored as decimal — the `.ui` rule) |
| `style` | any element | inline declaration list, wins over every rule |
| `class` | any element | accepted, inert (no selectors) |
| `min` / `max` / `step` / `value` | gauge / knob / meter | range/value properties (full integers, negatives accepted; the widget clamps `value` into [`min`, `max`] and collapses a reversed range to a point; `step` stays a non-negative magnitude) |
| `checked` | checkbox / radio / toggle | boolean, by presence |
| `group` | radio | radio group id (integer, any sign — equality-matched, never indexed) |
| `viewBox` | svg / vectordial | four viewBox units (`minx miny w h`, space/comma separated, decimals round half away from zero); malformed, absent, or non-positive size = pixel units (coordinates map 1:1) |

## Whitelist — CSS properties (inline `style=` and `<style>` rules)

| Property | Values | Mapping |
|---|---|---|
| `display` | `flex`, `block`, `none` | `none` → `visible=false`; `flex` on a `div` selects the `row` direction (see above); any other value leaves the element's table type unchanged |
| `flex-direction` | `row`, `column` | container type of a `div` |
| `width` / `height` | `Npx`, `N%` (1..100), `auto` | `Npx` → existing pixel size; `N%` → the `"N%"` percent form (FlexPanel parent content box); `auto` → absent (measured) |
| `flex` | `none`, or 1–3 integer tokens (`grow [shrink [basis]]`) | the `flex_grow` / `flex_shrink` / basis channels (`none` = `0 0 auto`); a single number keeps basis-auto and shrink-0 (documented CSS deviations); malformed tokens warn once, keep current |
| `flex-grow` / `flex-shrink` | integer ≥ 0 | surplus share / deficit weight; malformed warns once, keeps current |
| `flex-basis` | `auto`, `Npx`, `N%` (1..100) | the line-claim override (packing, wrap breaks, grow base); malformed warns once, keeps current |
| `justify-content` | `flex-start`/`start`/`left`, `center`, `flex-end`/`end`/`right`, `space-between`, `space-around` | main-axis placement over settled sizes (`spacing` is the minimum gap; `F ≤ 0` falls back to `start`); unknown warns once, keeps current |
| `align-items` | `flex-start`/`start`, `center`, `flex-end`/`end`, `stretch` | container cross default; **absent on an HTML container means `stretch`** (the CSS default — sections fill the chassis width); `.ui`/programmatic containers keep the `start` default; `baseline`/unknown warns once, keeps current |
| `align-self` | `auto` plus the five above | per-item override (`auto` inherits); honored only under a flex parent |
| `gap` | `Npx` (single value) | `spacing` |
| `padding` | `Npx` (single value) | `padding` |
| `margin` | 1–4 `Npx`/bare values (CSS side mapping) | `margin_t/r/b/l` node props; margins add to gaps, never collapse, never shrink |
| `margin-top` / `-right` / `-bottom` / `-left` | `Npx`/bare | wins over the shorthand; negative/`auto`/malformed warns, keeps 0 |
| `flex-wrap` | `wrap` | `wrap=true` |
| `aspect-ratio` | `W / H`, `N`, `auto` | derived-axis size from the cross axis (only when the derived axis is auto with no percent and the cross axis is explicit or percent — e.g. `.vu`'s `2/1` turns a `width: 100%` into a height; explicit declarations always win; the cross value may settle during the same layout — convergent passes re-read it, H-9); `auto`/malformed = absent |
| `background-color` | `#rgb`, `#rrggbb`, named subset, `rgb()`, `rgba()`, `transparent` | the shared `background` property (see design-file) |
| `background` | solid color form, `linear-gradient()` (3 stops keep the mid-section form; 4–8 stops land with positions — absent distribute evenly, ends default 0/100, clamped non-decreasing; 2 and >8 stops → first+last; `90/270deg`/`to left/right` = horizontal, `0/180deg`/`to top/bottom` = vertical), `radial-gradient(circle at X% Y%, A p%, B q%)`, `conic-gradient(from Ndeg, C0 P0[, C1 P1...])` (2–4 stops, positions in `deg`/bare 0–360, first 0 last 360 after clamping; center fixed 50%/50%), `repeating-linear-gradient()` (topmost layer paints as a translucent overlay over the base: up to 6 px stops, double-position `C A B` pairs, period = last stop) | comma layers split paren-aware; scanned base-first (last layer first), first supported non-repeating form wins as the base — `.amp`'s brushed layer now lands as the overlay instead of falling through; `url(` skips with a warning; unknown angles/px offsets/ellipse/`at` center/>4 conic stops/`%` conic or repeating positions/px linear mids/bare repeating stops skip the layer the same way |
| `border` | `Npx solid <color>` | `set_border`; any other style/grammar drops the border |
| `border-top` | `Npx solid <color>` | full-width top band over the background; radius corners not cut; other sides are off-whitelist |
| `opacity` | number `0..1`, `N%` | build-time fold into the widget's own paint alphas (bg/stops/border/text); descendants are not composited |
| `box-shadow` | `[inset] OXpx OYpx [blur] [spread] <color>`, comma list | inset = inner depth bands (full effect); outer = silhouette under the box (invisible on opaque boxes — no overdraw yet); max 2 + 2, extras warn-and-drop |
| `border-radius` | `Npx` (single), `50%` | px, or half the smaller side at draw time; multi-value drops |
| `position` | `relative`, `absolute` | `relative` = in-flow + containing block for abs descendants (its own offsets ignored); `absolute` = out of flow (code-contract P-3); anything else = static |
| `top` / `left` / `right` / `bottom` | `Npx`, `N%`, bare `0`, `auto` | abs offsets against the containing-block content box (`auto` = unset; only read on absolutely positioned elements) |
| `transform` | `translate(X[, Y])` (`%` of self or px) | shift after abs placement; any other function drops the declaration |
| `color` | same color forms | the shared `color` property (text color) |
| `font-size` | `Npx` | per-widget pixel size (`set_font_size`, code-contract §2.4): `N` clamps to 1..128, out-of-range/missing-family warns once and keeps the current provider; explicit per element, never inherited; ignored without `IMCORE_HAS_TTF_RUNTIME` (documented degradation) |
| `letter-spacing` | `Npx` | per-code-unit tracking in measure and draw (trailing unit included, per CSS); negative clamps to 0 |
| `font-weight` | `bold`, or a number ≥ 600 → on; `normal` / < 600 → off | double-strike: second pass shifted +1px, no bold variant |
| `text-shadow` | `DXpx DYpx [blur] <color>` | one solid offset copy drawn first; blur parsed-and-ignored; a comma list keeps the first shadow only |

## `<style>` rule matching

- CSS `/* */` comments are stripped in declaration lists: a comment
  never glues to its neighbor declaration (an uncommented parse would
  silently drop the declaration after the comment).
- A rule set is a flat list of `selector { declarations }`. A selector
  is a comma group of chains; each chain is one or more compounds in
  descendant order (`A B` = a B inside an A). A compound is an optional
  tag name plus `#id` and `.class` parts in any order (`div.model`,
  `.knob.a`, `#status`). Tags fold case (HTML), ids and classes keep
  theirs. `*`, `>`, `+`, `~`, `[…]`, and anything with `:` or `::`
  (pseudo-classes/elements) keep the whole rule inert — except exact
  `:root`, which only collects `--*` variables (below) and never
  matches an element. Structural tags (`html`, `head`) match
  nothing: no widget is ever built for them. `body` matches its element
  (see the root rules above); it still never builds except as the kept
  document root.
- Application is a real cascade, then inline: every matching rule
  contributes its declarations ordered by specificity `(ids, classes,
  tags)` and then document order (later wins ties); the inline
  `style=` attribute crowns everything. No inheritance — a child never
  inherits a parent's `color` (a descendant selector still has to match
  it explicitly).
- `!important` (ASCII case-insensitive, whitespace tolerated:
  `color: red !important`) lifts a declaration above every normal one;
  among important declarations the same specificity-then-order applies.
  Unknown properties stay ignored (with the LW warning) even when
  marked important.
- Custom properties: a `:root` rule's `--name: value` entries form the
  document's variable map (`--name` keeps its case; later rules win).
  `var(--name)` / `var(--name, fallback)` substitute textually in any
  declaration value (stylesheet and inline alike; one nesting level
  through fallbacks and chained variables). An unknown name without a
  fallback drops its declaration silently (malformed-value tolerance).

## SVG subset (`svg` / `vectordial`)

Only the instrument-dial shapes the demo documents use — everything
else in SVG is out of scope (full path/fill/stroke model stays in
backlog H-6):

- `viewBox="minx miny w h"` maps the viewBox onto the widget bounds by
  stretch (integer truncation toward zero; `preserveAspectRatio` is
  accepted but only `none` is honored — any other value warns and
  still stretches). Coordinates accept decimals, rounded half away
  from zero (SVG authors write `2.5`); malformed numbers drop their
  line/text.
- `line x1 y1 x2 y2`: drawn through `draw_line_aa`. `stroke` takes the
  shared color forms (absent = no stroke = the line is dropped, per
  SVG); `stroke-width` is parsed but rendered 1px (no thick-stroke
  primitive yet — accepted for forward compatibility, `0` drops the
  line); `stroke-linecap` accepted and ignored; `opacity="0..1"`
  scales the stroke alpha (at 16bpp any non-zero alpha plots per the
  binary policy, so ghost strokes stay visible).
- `text x y`: the element content drawn with the widget text seam
  (provider fallback chain included); `x/y` is the baseline start,
  `fill` defaults to the theme text, `text-anchor` selects
  start/middle/end, `font-family` is accepted and ignored; item-level
  `font-size` inside `svg` is accepted and ignored (per-`svg_text`
  sizes stay deferred — the `svg` element's own `font-size` sizes the
  whole canvas, code-contract §2.4).
- `g` never builds: inside `svg` it is transparent and folds
  `stroke`/`stroke-width`/`stroke-linecap`/`opacity`/`fill`/
  `text-anchor` onto its descendant `line`/`text` (nearest ancestor
  wins, the element's own attribute wins over all). Outside `svg`,
  `g`/`line`/`text` are off-whitelist elements (skipped with content
  dropped, like every non-table tag).
- `svg` is meaningful as a container child (it sizes through the
  shared `width`/`height` lengths); nested inside a text element it
  falls into the leaf-children rule (dropped with a warning).

## Deliberate deviations from HTML
- `div` is a content-measuring flex container, not a block box; its
  direction defaults to `column`, with `flex-direction: row` or a bare
  `display: flex` (the CSS flex default) selecting `row`.
- HTML containers stretch auto-cross children when `align-items` is
  absent (the CSS `stretch` default); `.ui`/programmatic containers
  keep the `start` default. Explicit/percent cross axes keep their size
  (the CSS non-auto rule). A single line fills the content cross box,
  so `center`/`end` place within the real extent and stretch fills the
  container; wrapped lines keep stacking from the padding origin.
- No text flow: `p`/`span` are single-line labels, `br` is a one-line
  spacer — real paragraph reflow waits for H-1.
- Entities are only the six named above (`&nbsp;` folds to a space).
- `meter` is a `progress_bar`.
- Widgets are the presentation: alignment, focus, and interaction follow
  the widget, not CSS.

## Cross-cutting invariants

- **Static by contract** (the design-file rule): no JS, no dynamic
  behavior, no callbacks in the document. Event wiring happens in C++
  after materialization via `find_by_id`, exactly like `.ui`.
- **Determinism unchanged** (§4.11): the parse runs once at load; nothing
  in the path introduces timers, threads, or background work.

## Adding to the whitelist

A new element or property is a contract change: add the row here (and in
the design-file property table when it is a shared property), implement
the mapping and tag-table entry, lock it with a test — the `ui_builder`
tag-table precedent. Follow-ups H-1..H-7 each grow the whitelist.