<style>
.im-tabs{display:flex;gap:4px;border-bottom:1px solid #d0d7de;margin-bottom:16px;flex-wrap:wrap}
.im-tabs input{display:none}
.im-tabs label{display:inline-block;padding:6px 14px;margin:0;cursor:pointer;font-size:14px;font-weight:600;color:#57606a;border:1px solid transparent;border-bottom:none;border-radius:6px 6px 0 0}
.im-tabs input:checked+label{color:#1f2328;background:#f6f8fa;border:1px solid #d0d7de;border-bottom-color:#f6f8fa;margin-bottom:-1px}
.im-panel{display:none}
#t-en:checked~#p-en,#t-zh:checked~#p-zh,#t-ja:checked~#p-ja{display:block}
</style>

# Imprint

<div class="im-tabs">
<input type="radio" id="t-en" name="lang" checked>
<label for="t-en">English</label>
<input type="radio" id="t-zh" name="lang">
<label for="t-zh">中文</label>
<input type="radio" id="t-ja" name="lang">
<label for="t-ja">日本語</label>
</div>

<div id="p-en" class="im-panel">

**One pixel buffer, every target.** Imprint is a dependency-free, software-rendered C++17 UI framework. The same source tree runs on a Nintendo DS, Linux (framebuffer or X11), Windows, in the browser via WebAssembly, and behind a C-ABI driven from Python.

| Windows | Linux (X11) | WebAssembly |
|:---:|:---:|:---:|
| <img src="assets/win.png" width="240"> | <img src="assets/linux_x11.png" width="240"> | <img src="assets/wasm.png" width="160"> |

| Nintendo DS | Python host |
|:---:|:---:|
| <img src="assets/nds.png" width="240"> | <img src="assets/py256.png" width="240"> |

## Highlights

- **Retained-mode widget tree** — `Button`, `Label`, `Dialog`, `FlexPanel`, `GraphicsView` and more
- **Software rendering into a raw pixel buffer** — no GPU, no external rendering library; the buffer format is fixed at build time (`COLOR_DEPTH`)
- **Deterministic repaint-on-demand** — dirty tracking, shell owns the loop, no hidden redraws
- **C-ABI as a first-class citizen** — stable `zbapi` C interface with Python (ctypes), WebAssembly and C smoke-test hosts
- **Embedded-grade** — no RTTI, 16-bit color (abgr1555), integer-only geometry option, non-atomic refcounting option (NDS has no libatomic)
- **Zero-allocation hot paths** — RAII `ClipGuard`, event tombstoning, `Subscription`
- **UTF-8 text throughout** — built-in 5x7 bitmap glyph fallback; optional FreeType / libpng / libjpeg
- **C++17, CMake, static libraries** — everything is composable, nothing is forced

## Platforms

| Platform | Shell | Notes |
|---|---|---|
| Nintendo DS | `imshell/nds` | ARM9, 4 MB RAM, no FPU; ROM packaged with ndstool |
| Linux | `imshell/fb`, `imshell/x11` | framebuffer or X11 window |
| Windows | `imshell/win` | Win32, 32-bit BGRA or 16-bit |
| WebAssembly | `demo/wasm` | emscripten + canvas |
| Python | `demo/python` | ctypes + pygame over the C-ABI |

## Build

| Target | Command |
|---|---|
| Windows (MSVC) | `cmake -S . -B build && cmake --build build` |
| Linux (framebuffer) | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=FB` |
| Linux (X11) | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=X11` |
| NDS (docker) | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build_nds'` |
| WebAssembly | `demo/wasm/build.sh` (docker emscripten) |
| Python | build `binding` shared lib, then `python3 demo/python/myapp.py --lib <libzbapi>` |

Tests: `test/test_imui.exe` (17 suites, plain asserts, no framework). Automatic on desktop builds; skipped on NDS.

## Demo

The demo app is a TicTacToe game (human vs computer), exercising dialogs, buttons, layout and repaint-on-demand. The NDS build produces `build_nds/bin/tictactoe.nds`.

## Repository layout

| Path | Contents |
|---|---|
| `imcore/` | drawing kernel: Graphics / Color / Font / Image |
| `imui/` | widget library (retained-mode) |
| `imapp/` | app interface: `IApp` / `IWindow` + `CanvasWindow` default |
| `imevent/` | events & input |
| `imutil/` | logging |
| `binding/` | C-ABI `zbapi` shared library + C smoke test |
| `imshell/` | platform shells (NDS / FB / X11 / Win) |
| `apps/` | demo applications |
| `test/` | unit tests |

## License

[MIT](LICENSE) © 2026 tyou hyou

</div>

<div id="p-zh" class="im-panel">

**一个像素缓冲，跑遍所有目标。** Imprint 是一个零依赖、软件渲染的 C++17 UI 框架。同一份源码可以跑在任天堂 DS、Linux（framebuffer 或 X11）、Windows、浏览器（WebAssembly）以及通过 C-ABI 调用的 Python 宿主上。

| Windows | Linux (X11) | WebAssembly |
|:---:|:---:|:---:|
| <img src="assets/win.png" width="240"> | <img src="assets/linux_x11.png" width="240"> | <img src="assets/wasm.png" width="160"> |

| 任天堂 DS | Python 宿主 |
|:---:|:---:|
| <img src="assets/nds.png" width="240"> | <img src="assets/py256.png" width="240"> |

## 特性

- **保留模式控件树** — `Button`、`Label`、`Dialog`、`FlexPanel`、`GraphicsView` 等
- **软件渲染到原始像素缓冲** — 不需要 GPU，不需要外部渲染库；缓冲格式在构建期由 `COLOR_DEPTH` 固定
- **确定性的按需重绘** — 脏标记追踪，主循环归壳层所有，没有隐藏的重绘
- **C-ABI 一等公民** — 稳定的 `zbapi` C 接口，配 Python（ctypes）、WebAssembly 和 C 冒烟测试宿主
- **嵌入式级约束** — 无 RTTI、16 位色（abgr1555）、纯整数几何选项、非原子引用计数选项（NDS 没有 libatomic）
- **零分配热路径** — RAII `ClipGuard`、事件墓碑删除、`Subscription`
- **全链路 UTF-8 文本** — 内置 5x7 位图字形兜底；可选 FreeType / libpng / libjpeg
- **C++17、CMake、静态库** — 一切可组合，不强加任何东西

## 平台

| 平台 | 壳层 | 说明 |
|---|---|---|
| 任天堂 DS | `imshell/nds` | ARM9、4MB 内存、无 FPU；ndstool 打包 ROM |
| Linux | `imshell/fb`、`imshell/x11` | framebuffer 或 X11 窗口 |
| Windows | `imshell/win` | Win32，32 位 BGRA 或 16 位 |
| WebAssembly | `demo/wasm` | emscripten + canvas |
| Python | `demo/python` | 通过 C-ABI 使用 ctypes + pygame |

## 构建

| 目标 | 命令 |
|---|---|
| Windows（MSVC） | `cmake -S . -B build && cmake --build build` |
| Linux（framebuffer） | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=FB` |
| Linux（X11） | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=X11` |
| NDS（docker） | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build_nds'` |
| WebAssembly | `demo/wasm/build.sh`（docker emscripten） |
| Python | 先构建 `binding` 动态库，再 `python3 demo/python/myapp.py --lib <libzbapi>` |

测试：`test/test_imui.exe`（17 个套件，纯断言，无测试框架）。桌面构建自动运行；NDS 跳过。

## 示例

示例应用是井字棋（人机对战），覆盖对话框、按钮、布局与按需重绘。NDS 构建产出 `build_nds/bin/tictactoe.nds`。

## 仓库结构

| 路径 | 内容 |
|---|---|
| `imcore/` | 绘制内核：Graphics / Color / Font / Image |
| `imui/` | 控件库（保留模式） |
| `imapp/` | 应用接口：`IApp` / `IWindow` + 默认 `CanvasWindow` |
| `imevent/` | 事件与输入 |
| `imutil/` | 日志 |
| `binding/` | C-ABI `zbapi` 动态库 + C 冒烟测试 |
| `imshell/` | 平台壳层（NDS / FB / X11 / Win） |
| `apps/` | 示例应用 |
| `test/` | 单元测试 |

## 许可证

[MIT](LICENSE) © 2026 tyou hyou

</div>

<div id="p-ja" class="im-panel">

**一つのピクセルバッファで、すべてのターゲットへ。** Imprint は依存ゼロ・ソフトウェアレンダリングの C++17 UI フレームワークです。同じソースツリーが、ニンテンドーDS、Linux（フレームバッファまたは X11）、Windows、ブラウザ（WebAssembly）、そして C-ABI 経由の Python ホストで動作します。

| Windows | Linux (X11) | WebAssembly |
|:---:|:---:|:---:|
| <img src="assets/win.png" width="240"> | <img src="assets/linux_x11.png" width="240"> | <img src="assets/wasm.png" width="160"> |

| ニンテンドーDS | Python ホスト |
|:---:|:---:|
| <img src="assets/nds.png" width="240"> | <img src="assets/py256.png" width="240"> |

## 特徴

- **保持モードのウィジェットツリー** — `Button`、`Label`、`Dialog`、`FlexPanel`、`GraphicsView` など
- **生のピクセルバッファへのソフトウェア描画** — GPU 不要、外部レンダリングライブラリ不要。バッファ形式はビルド時に `COLOR_DEPTH` で固定
- **決定的なオンデマンド再描画** — ダーティトラッキング、メインループはシェルが所有、隠れた再描画なし
- **C-ABI を第一級市民として** — 安定した `zbapi` C インターフェースに、Python（ctypes）、WebAssembly、C スモークテストのホスト
- **組み込みグレード** — RTTI なし、16 ビットカラー（abgr1555）、整数専用ジオメトリオプション、非アトミック参照カウントオプション（NDS に libatomic なし）
- **ゼロアロケーションのホットパス** — RAII の `ClipGuard`、イベントのトゥームストーン、`Subscription`
- **テキストは全体で UTF-8** — 組み込みの 5x7 ビットマップグリフフォールバック。FreeType / libpng / libjpeg はオプション
- **C++17、CMake、静的ライブラリ** — すべて組み合わせ可能、強制されるものはなし

## プラットフォーム

| プラットフォーム | シェル | 備考 |
|---|---|---|
| ニンテンドーDS | `imshell/nds` | ARM9、4MB RAM、FPU なし。ndstool で ROM 化 |
| Linux | `imshell/fb`、`imshell/x11` | フレームバッファまたは X11 ウィンドウ |
| Windows | `imshell/win` | Win32、32 ビット BGRA または 16 ビット |
| WebAssembly | `demo/wasm` | emscripten + canvas |
| Python | `demo/python` | C-ABI 経由で ctypes + pygame |

## ビルド

| ターゲット | コマンド |
|---|---|
| Windows（MSVC） | `cmake -S . -B build && cmake --build build` |
| Linux（フレームバッファ） | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=FB` |
| Linux（X11） | `cmake -S . -B build_linux -DIM_SHELL_BACKEND=X11` |
| NDS（docker） | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build_nds'` |
| WebAssembly | `demo/wasm/build.sh`（docker emscripten） |
| Python | `binding` 共有ライブラリをビルドしてから `python3 demo/python/myapp.py --lib <libzbapi>` |

テスト：`test/test_imui.exe`（17 スイート、素の assert、テストフレームワークなし）。デスクトップビルドで自動実行、NDS ではスキップ。

## デモ

デモアプリは三目並べ（人間 vs コンピュータ）。ダイアログ・ボタン・レイアウト・オンデマンド再描画を一通り使います。NDS ビルドは `build_nds/bin/tictactoe.nds` を生成します。

## リポジトリ構成

| パス | 内容 |
|---|---|
| `imcore/` | 描画カーネル：Graphics / Color / Font / Image |
| `imui/` | ウィジェットライブラリ（保持モード） |
| `imapp/` | アプリインターフェース：`IApp` / `IWindow` + デフォルト `CanvasWindow` |
| `imevent/` | イベントと入力 |
| `imutil/` | ロギング |
| `binding/` | C-ABI `zbapi` 共有ライブラリ + C スモークテスト |
| `imshell/` | プラットフォームシェル（NDS / FB / X11 / Win） |
| `apps/` | デモアプリケーション |
| `test/` | ユニットテスト |

## ライセンス

[MIT](LICENSE) © 2026 tyou hyou

</div>
