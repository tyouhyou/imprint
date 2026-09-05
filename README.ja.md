# Imprint UI

> 本ファイルは英語版 README の翻訳です。内容は [README.md](README.md) が正（2026-08-30 時点）。

[![English](https://img.shields.io/badge/English-lightgrey)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-lightgrey)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-blue)](README.ja.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20NDS%20%7C%20WASM%20%7C%20Python-lightgrey.svg)]()

**GUI は一度書くだけ。どこでも動く——ニンテンドーDS でも。**

Imprint UI は、組み込みと非典型的なターゲットのための、極小・依存ゼロ・ソフトウェアレンダリングの C++17 UI フレームワークです。同じ UI ソースツリーが Windows、Linux、macOS、ブラウザ（WebAssembly）、ニンテンドーDS 向けにコンパイルできます——PC で開発・プレビューし、**まったく同じコード**をデバイスへ。

**1 つの UI ソースツリー。1 つのピクセルバッファ。複数のターゲット。**

![1 つの UI ソースツリー、4 つのターゲット](assets/showcase/montage.png)

**[ブラウザでそのまま試す](https://tyouhyou.github.io/imprint/)** —— 上のページは WebAssembly ビルド。ニンテンドーDS のフレームは同じソースの devkitARM ビルドです。

<img src="assets/showcase/showcase.gif" width="480" alt="フレームごとに記録した showcase：START でプログレスバーが充填、ダークテーマ、全ウィジェットページ、スライダーがデモバーを駆動">

GPU 不要。OS の GUI ツールキット不要。プラットフォーム固有の UI コードも不要。

```
              同じ UI ソース
                    │
        ┌───────────┼───────────┐
        ↓           ↓           ↓
     Windows      Linux       macOS
        │        (X11/FB)       │
        └───────────┼───────────┘
                    ↓
             WebAssembly  ←  ブラウザで試す
                    ↓
              ニンテンドーDS
                    ↓
        あなたの組み込みボード（C-ABI）
```

上記 `showcase` アプリの実測フットプリント（Release ビルド）：

| ターゲット | UI コード+データ | RAM（静的） | フレームバッファ | 配置サイズ |
|---|---|---|---|---|
| ニンテンドーDS | 543 KB text + 11 KB data | 7.7 KB BSS | 96 KB（256×192×2 B） | 646 KB `.nds` |
| WebAssembly | — | — | 256×192×4 B | 250 KB の単一 `.js`、`file://` で直接動作 |

## 特徴

- **保持モードのウィジェットツリー** — `Button`、`Label`、`Dialog`、`FlexPanel`、`GraphicsView` など
- **デザインファイル** — 極小テキスト形式（`.ui`）で UI を記述し、ビルド時に検証して C 配列にパック。どのターゲットでも配列からロード。プレビューアプリはファイルを直接描画
- **生のピクセルバッファへのソフトウェア描画** — GPU 不要、外部レンダリングライブラリ不要。バッファ形式はビルド時に `COLOR_DEPTH` で固定
- **決定的なオンデマンド再描画** — ダーティトラッキング、メインループはシェルが所有、隠れた再描画なし
- **C-ABI を第一級市民として** — 安定した `zbapi` C インターフェースに、Python（ctypes）、WebAssembly、C スモークテストのホスト
- **契約による自動化親和性** — 「ホストがすべてを駆動する」モデルにより、スクリプトがユーザーの代わりを務められる：入力を与え、フレームをポンプし、ピクセルにアサート。シングルスレッドでタイマーなしのためドライバに sleep 不要——テストバッテリーには公開 API のみで駆動するエンドツーエンドの `automation` スイートを含む
- **組み込みグレード** — RTTI なし、16 ビットカラー（abgr1555）、整数専用ジオメトリオプション、非アトミック参照カウントオプション（NDS に libatomic なし）
- **ゼロアロケーションのホットパス** — RAII の `ClipGuard`、イベントのトゥームストーン、`Subscription`
- **テキストは全体で UTF-8** — 組み込みの 5x7 ビットマップグリフフォールバック（ソース文字列から自動サブセット化）。FreeType（フォント）と vendored stb コーデック（PNG/JPEG）はオプション
- **C++17、CMake、静的ライブラリ** — すべて組み合わせ可能、強制されるものはなし

## 非目標

GPU 描画アクセラレーション（レンダリングカーネルは CPU ソフトウェアラスタライズのまま）·
アニメーション/トランジションシステム · 実行時バックエンド切替 · マルチスレッド描画 ·
IME 合成 · RTL レイアウト。Imprint UI は意図的に極小を保ちます：1 つのウィジェットツリー、
1 つのピクセルバッファ、1 つの入力ストリーム——それ以外はホストの仕事です。

## クイックサンプル

```cpp
#include "imapp.hpp"
#include "imui.hpp"

int main()
{
    auto app = zb::app::make_app();
    app->create_window(320, 240);
    auto* win = static_cast<zb::app::CanvasWindow*>(app->window().get());

    auto btn = std::make_unique<zb::ui::Button>();
    btn->set_size(100, 40);
    btn->set_text("クリック");
    btn->clicked += [] { printf("こんにちは！\n"); };
    win->root().add_child(std::move(btn));

    app->paint();
}
```

同じ画面をデザインファイルで記述（`tools/examples/menu.ui`）：

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

ビルド時に `ui_embed` でパック（不正なファイルはビルドエラー）、実行時は
`parse_ui_text` + `build()` で実体化 — 全プラットフォーム同一コードパス。
プレビューはこのように：

```
UI_PREVIEW_FILES="tools/examples/menu.ui" cmake -B build/build_linux -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux
```

## ビルド

| ターゲット | コマンド | 備考 |
|---|---|---|
| Windows（MSVC） | `cmake -S . -B build/build_win && cmake --build build/build_win` | 依存ゼロのデフォルト（32bpp） |
| Windows + フォント | `cmake -S . -B build/build_font -DUSE_FONT=ON && cmake --build build/build_font` | 実行には `freetype.dll` が PATH 上に必要 |
| macOS（AppKit） | `cmake -S . -B build/build_mac && cmake --build build/build_mac` | deployment target 11.0、追加オプション不要 |
| Linux（X11） | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=X11 && cmake --build build/build_linux` | 入力対応バックエンド |
| Linux（フレームバッファ） | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux` | 表示のみ。操作は X11 で |
| ニンテンドーDS | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build/build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build/build_nds'` | `build/build_nds/bin/tictactoe.nds` を生成。`-DSTORY=showcase` でショーケース ROM をビルド（ホスト製の `ui_embed` と `asset_gen` を `-DUI_EMBED_EXECUTABLE=` / `-DASSET_GEN_EXECUTABLE=` で渡す必要あり） |
| WebAssembly | `demo/wasm/build.sh`（docker emscripten） | node スモークテスト付き |
| Python | `binding` 共有ライブラリをビルドしてから `SDL_VIDEODRIVER=dummy python3 demo/python/myapp.py --lib <libzbapi>` | ctypes + pygame ホスト |

テスト：`test/test_imui`——素の assert、テストフレームワークなし。デスクトップビルドで自動実行、NDS ではスキップ。

## ドキュメント

**推奨読書順序**（新しいメンテナーの初回）：
1. [`docs/getting-started.md`](docs/getting-started.md)——最初のアプリを動かして自分のものにする（約 5 分）
2. この README → **ビルド**（各ターゲットでバイナリを動かす）
3. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §1–§2——システムの全体像、モジュールマップと依存ルール
4. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §3–§5——規範的契約、ターゲット、既知の制限
5. [`docs/code-contract.md`](docs/code-contract.md)——API レベルのインターフェース契約
6. [`docs/design-file.md`](docs/design-file.md)——`.ui` ファイルを扱うときに読む

**タスク別の参照先**：公開 API に触れる → 先に `code-contract.md`（契約が API に先行）· 新ターゲット / 新ピクセルフォーマット / 新ビルドオプション → `docs/backlog.md` と ARCHITECTURE §4 · `.ui` 文法やパッケージング → `design-file.md` · C-ABI ホスト → `zbapi.h` + ARCHITECTURE §4.8 · ビルド/実行コマンド → 下の**ビルド**。

- [`docs/getting-started.md`](docs/getting-started.md)——クローンから自分のアプリまで：`hello` ストーリーの実行、`IApp`/`CanvasWindow` の継ぎ目の理解、自分の story の登録
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)——実装済みアーキテクチャ：モジュールマップと依存ルール、契約（フレームライフサイクル、入力、ピクセルモデル、テキスト、イベント、エラー処理、C-ABI ホスト、ビルドオプション）、既知の制限
- [`docs/backlog.md`](docs/backlog.md)——生きたバックログ：アーキテクチャ項目、製品機能バッチ（L/I/F）、条件トリガー項目
- [`docs/code-contract.md`](docs/code-contract.md)——API レベルのインターフェース契約：エラーパス、UTF-8/テキスト、glyph provider、ツリー変更、レイアウト無効化、アロケーション予算、プレゼンテーションシームのコンバータ
- [`docs/design-file.md`](docs/design-file.md)——`.ui` デザインファイル形式：文法、パッケージングパイプライン、実体化セマンティクス
- [`binding/include/zbapi.h`](binding/include/zbapi.h)——C-ABI ホストインターフェース。ホストルールは ARCHITECTURE §4.8

## デモ

**Hello**（`-DSTORY=hello`）——入門アプリ：ラベル 1 つとクリック回数を数えるボタン。コピーすれば自分のアプリの起点になります（[`docs/getting-started.md`](docs/getting-started.md) 参照）。

**showcase**（`-DSTORY=showcase`）——マルチターゲット・モンタージュの元になるウィジェットギャラリー。ダークで起動し、フレームワーク自身のラスタライザで描いたアニメーションチャート（角丸カード上のアンチエイリアス曲線＋グラデーション領域、app 側 tween が少しずつ描き出す）で開始。デバイス状態のコントロールパネル（プログレスバー、START/STOP、ダーク/ライトテーマ切替）と、アルファ資産合成付きの全ウィジェットページ（9-slice 影カード、アクセント色にティントしたボール。資産は `tools/asset_gen` がビルド時に生成）。`assets/showcase/` のフレームはこれらのビルドから生成。WASM 版はオンラインで遊べます（[tyouhyou.github.io/imprint](https://tyouhyou.github.io/imprint/)、ローカルでは `demo/wasm/build.sh showcase`）。同じソースが NDS ROM もビルドします。

**三目並べ**（デフォルト story）——人間 vs コンピュータ。ダイアログ・ボタン・レイアウト・オンデマンド再描画を一通り使います。NDS ビルドは `build/build_nds/bin/tictactoe.nds` を生成します。3 つ目のアプリ `ui_preview`（`-DSTORY=ui_preview`）は `UI_PREVIEW_FILES`（スペース区切りのパス、左右キーでドキュメント切替）のデザインファイルを描画します。

| Windows | macOS | Linux (X11) | WebAssembly | ニンテンドーDS | Python ホスト |
|:---:|:---:|:---:|:---:|:---:|:---:|
| <img src="assets/tictactoe/win.png" width="240"> | <img src="assets/tictactoe/mac.png" width="240"> | <img src="assets/tictactoe/linux_x11.png" width="240"> | <img src="assets/tictactoe/wasm.png" width="160"> | <img src="assets/tictactoe/nds.png" width="200"> | <img src="assets/tictactoe/py256.png" width="240"> |

## ライセンス

[MIT](LICENSE) © 2026 tyou hyou
