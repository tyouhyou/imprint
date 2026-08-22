#!/bin/sh
# Build the TicTacToe wasm demo inside the emscripten/emsdk container.
# Expected layout: the repo is mounted at /src (project root = /src).
set -e

cd /src

SRCS="
  imcore/src/core/graphics.cpp
  imcore/src/text/text_image.cpp
  imcore/src/text/utf8.cpp
  imcore/src/text/bitmap_provider.cpp
  imui/src/widget.cpp
  imui/src/panel.cpp
  imui/src/flex_panel.cpp
  imui/src/button.cpp
  imui/src/checkbox.cpp
  imui/src/radio_button.cpp
  imui/src/label.cpp
  imui/src/slider.cpp
  imui/src/list_box.cpp
  imui/src/text_input.cpp
  imui/src/dialog.cpp
  imui/src/dispatcher.cpp
  imui/src/ui_file.cpp
  apps/tictactoe/src/app_maker.cpp
  apps/tictactoe/src/ai.cpp
  apps/tictactoe/src/board.cpp
  apps/tictactoe/src/game.cpp
  apps/tictactoe/src/tictactoe.cpp
  apps/tictactoe/src/tictactoe_controller.cpp
  apps/tictactoe/src/tictactoe_view.cpp
  binding/src/zbapi.cpp
"

OUT="/src/demo/wasm/tictactoe.js"

# font subset (batch E): generate the table for the app/test sources, like
# the CMake configure step does; skipped when Python is missing
SUBSET_FLAGS=""
if python3 tools/font_subset.py --extras tools/extra_glyphs.py \
    --out /tmp/subset_glyphs.hpp /src/apps /src/test 2>/dev/null; then
    SUBSET_FLAGS="-DIMCORE_HAS_SUBSET -I/tmp"
fi

em++ -std=c++17 -O2 \
  -DCOLOR_DEPTH=32 -DRGB_MODEL=bgra32 -DENDIAN=le \
  $SUBSET_FLAGS \
  -I /src/imcore/include \
  -I /src/imcore/include/core \
  -I /src/imcore/include/text \
  -I /src/imcore/include/codec \
  -I /src/imui/include \
  -I /src/imevent/include \
  -I /src/iminput/include \
  -I /src/imutil/include \
  -I /src/imapp/include \
  -I /src/apps/tictactoe/include \
  -I /src/binding/include \
  -s EXPORTED_FUNCTIONS='["_zb_app_create","_zb_app_destroy","_zb_input","_zb_paint","_zb_buffer","_zb_set_painted_callback","_zb_set_log_callback","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAPU32"]' \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s EXIT_RUNTIME=1 \
  --no-entry \
  $SRCS -o "$OUT"

echo "=== build ok ==="
ls -l "$OUT" "${OUT%.js}.wasm"

# node smoke-test build: MODULARIZE + ENVIRONMENT=node so `require()` returns
# the module factory (the browser build above must stay non-modularized,
# main.js drives it through the global `Module` object)
SMOKE_OUT="/src/demo/wasm/tictactoe_mod.js"
em++ -std=c++17 -O2 \
  -DCOLOR_DEPTH=32 -DRGB_MODEL=bgra32 -DENDIAN=le \
  $SUBSET_FLAGS \
  -I /src/imcore/include \
  -I /src/imcore/include/core \
  -I /src/imcore/include/text \
  -I /src/imcore/include/codec \
  -I /src/imui/include \
  -I /src/imevent/include \
  -I /src/iminput/include \
  -I /src/imutil/include \
  -I /src/imapp/include \
  -I /src/apps/tictactoe/include \
  -I /src/binding/include \
  -s MODULARIZE=1 -s EXPORT_NAME=createTictactoe \
  -s ENVIRONMENT=node \
  -s EXPORTED_FUNCTIONS='["_zb_app_create","_zb_app_destroy","_zb_input","_zb_paint","_zb_buffer","_zb_set_painted_callback","_zb_set_log_callback","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8","HEAPU32"]' \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s INITIAL_MEMORY=16777216 \
  -s EXIT_RUNTIME=1 \
  --no-entry \
  $SRCS -o "$SMOKE_OUT"
echo "=== smoke build ok ==="
ls -l "$SMOKE_OUT" "${SMOKE_OUT%.js}.wasm"
