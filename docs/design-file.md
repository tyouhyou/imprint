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

Desktop preview without writing an app:

```
UI_PREVIEW_FILES="tools/examples/menu.ui" cmake -B build/build_linux -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux
```

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
  `true|false`.
- `id=` names a node for later lookup (`find_by_id`); an unquoted
  integer value is accepted and stored as its decimal string.
  `items=` accepts multiple space-separated quoted strings.
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
`list_box`, `text_input`), with properties including `id`, `text`,
`size`, `pos`, `named`, `checked`, `group`, `step`, `rows`, `spacing`,
`padding`, `wrap`, `flex`, `visible`. Both the fluent builder and the
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
- `text` is accepted as UTF-8 and stored internally as UTF-16.

## What a design file cannot express

The layer is deliberately static. Design files carry structure,
properties, and ids only — never:

- dynamic models (e.g. `ListBox` item-text callbacks),
- event wiring,
- font/glyph content,
- runtime-generated text.

Events are wired after materialization: look widgets up with
`find_by_id` and subscribe to their events in code.
