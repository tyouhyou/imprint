# Imprint Design Files (`.ui`)

A design file describes a **static** widget tree in a small plain-text
format. The same file is consumed on every target: validated and packed
into a C byte array at build time, parsed at runtime into the shared
`ui_node` intermediate representation, and materialized into a live
widget tree by `build()`. The layer's role in the architecture is
described in `docs/ARCHITECTURE.md` §4.10.

## Pipeline

```
menu.ui ──► tools/ui_embed ──► C byte array (embedded_ui_file / find_ui_file)
                │  validates at build time; an invalid file fails the build
                ▼
        parse_ui_text(bytes) ──► ui_node ──► build(host, root) ──► widget tree
```

Desktop preview without writing an app: the `ui_preview` story with
`UI_PREVIEW_FILES="<paths>"` — exact commands live in README "Build"
(the single home for build/run commands).

`UI_PREVIEW_FILES` takes space-separated paths; left/right keys switch
documents.

## Grammar

- A document is a sequence of nodes; **indentation defines nesting**
  (one tab = 4 columns). A deeper-indented line becomes a child of the
  line above it; skipping levels is legal.
- A node is a `tag` followed by zero or more `key=value` properties.
  Bare values are forbidden — everything is `key=value`.
- Strings are double-quoted `"..."`; escapes are `\"` and `\\` (any
  other `\x` stays literal). Integers are bare; booleans are
  `true|false`. Percent sizes are `N%` (N a non-negative integer, no
  sign or fraction) and may be quoted or bare — `width="50%"` and
  `width=50%` are the same value.
- `id=` names a node for later lookup (`find_by_id`); an unquoted
  integer value is accepted and stored as its decimal string.
  `items=` accepts multiple space-separated quoted strings.
- `halign=` accepts the quoted values `left`, `center`, or `right`;
  `valign=` accepts `top`, `center`, or `bottom`. These values control
  text placement inside the widget's allocated bounds.
- A line starting with `#` is a comment.
- **Continuation**: a single backslash at end of line joins the next
  physical line (the marker and newline are removed, the next line's
  leading indentation is stripped, blank/comment lines in between are
  skipped). An *even* number of trailing backslashes is a literal
  backslash, not a continuation.
- A line with an unclosed string is discarded whole.
- Unknown tags, unknown properties, and malformed values are logged as
  warnings and skipped — **parsing never fails** on bad content
  (`parse_ui_text` reports failure only for a document with no nodes).

Example (`tools/examples/menu.ui`):

```
column id="root" spacing=6 padding=10
  label id="title" text="Settings"
  checkbox id="sound" text="Sound"
  slider min=0 max=100 step=10
  list_box rows=3 items="Easy" "Normal" "Hard"
  row spacing=4
    button id="ok" text="OK"
    button id="cancel" text="Cancel"
```

The tag and property set is defined by the factory/property tables in
`imui/src/ui_builder.cpp`: containers (`panel`, `column`, `row`) and
widgets (`label`, `button`, `checkbox`, `radio`, `slider`,
`progress_bar`, `toggle`, `gauge`, `knob`, `trend`, `list_box`,
`text_input`, `svg`), with properties including
`id`, `text`, `items`, `width`, `height` (px, or `N%` — see below),
`pos_x` / `pos_y` (pixel position), `valign` (`top`, `center`, `bottom`),
`halign` (`left`, `center`, `right`), `checked`, `group`, `step`,
`min`, `max`, `value`, `rows`, `spacing`, `padding`,
`padding_t` / `padding_r` / `padding_b` / `padding_l` (per-side wins
over the uniform share), `wrap`, `flex` (integer grow weight),
`margin_t`, `margin_r`, `margin_b`, `margin_l`,
`visible`, `background`, `color`, `font_size`,
`text_wrap` (bool: word-wrap the label/paragraph block),
`line_h` (int: explicit line pitch, 0 = provider default),
`justify` (0–4: FlexPanel main-axis justify code),
`align` (0–3: FlexPanel cross-axis align code — start/center/end/stretch),
`letter_px` (int: per-code-unit tracking px), `bold` (bool: double-strike),
`shadow_color` + `shadow_dx` + `shadow_dy` (one solid text-shadow copy),
`aspect_w` + `aspect_h` (H-5 aspect ratio pair),
`position` (`relative` / `absolute`) with `abs_l` / `abs_t` / `abs_r` /
`abs_b` / `translate_x` / `translate_y` offsets (P-3),
`elem_opacity` (0–1000: fixed-point paint alpha fold). Unknown
properties are silently tolerated (only unknown tags warn) — the
fluent builder's `size()`/`pos()`/`named()` methods are **not** text
keys (`size`/`pos` emit `width`/`height`/`pos_x`/`pos_y` props;
`named` sets `id`). A `width`/`height` value of the form
`N%` (1..100) declares
that axis as a percentage of the FlexPanel parent's content box,
resolved at layout time — it never becomes an explicit size, and outside
a FlexPanel it stays unresolved. `background` and `color` carry color
values as quoted strings in the forms `#rgb`, `#rrggbb`, the named
subset (`transparent`, `black`, `white`, `red`, `green`, `blue`,
`yellow`, `gray`/`grey`, `cyan`, `magenta`); a malformed color is
silently dropped (nothing is set — the tolerated-value rule).
`background` sets the widget's background color, `color` its text color;
`transparent` for either is a no-op (the default "no background / theme
text" stays). `font_size` is a bare integer pixel size (1..128,
`set_font_size`, code-contract §2.4) — out-of-range values and builds
without `IMCORE_HAS_TTF_RUNTIME` keep the current provider per the
tolerance rule. Both the fluent builder and the
parser feed the same tables, so anything expressible in C++ builder
form parses identically from text.

## Root handling

- A document with exactly one top-level container (`panel`/`column`/
  `row`) returns that node directly; its container properties
  (`spacing`, `padding`, `wrap`) apply to the build host.
- Otherwise a pseudo-root `type="root"` wraps the top-level nodes; its
  children are materialized into the host one by one.

## Materialization semantics

- Host is a `FlexPanel`: child `flex_grow` values drive flex layout;
  otherwise children follow the `Panel` linear layout.
- Unknown tag: logged and skipped. Children under a non-container tag:
  dropped with a warning.
- Missing property or type mismatch: default value, silently — the
  parse path never throws.
- A percent `width`/`height` (`N%`) is a layout-time declaration on that
  axis, resolved by the FlexPanel parent against its content box (fixed
  siblings claim their space first; percent siblings that overflow the
  remaining space are scaled into it proportionally; `flex=` on the same
  child is ignored); under a non-flex parent it stays unresolved and the
  axis keeps its current size.
- `text` is accepted as UTF-8 and stored internally as UTF-16.

## What a design file cannot express

The layer is deliberately static. Design files carry structure,
properties, and ids only — never:

- dynamic models (e.g. `ListBox` item-text callbacks),
- event wiring,
- font/glyph content (font bytes, family names — the `font_size`
  metric is allowed),
- runtime-generated text.

Events are wired after materialization: look widgets up with
`find_by_id` and subscribe to their events in code.
