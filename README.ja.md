# Imprint

[![English](https://img.shields.io/badge/English-lightgrey)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-lightgrey)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-blue)](README.ja.md)


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
