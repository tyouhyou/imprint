# Imprint UI

> 本ファイルは英語版 README の翻訳です。内容は [README.md](README.md) が正（2026-09-22 時点）。

[![English](https://img.shields.io/badge/English-lightgrey)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-lightgrey)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-blue)](README.ja.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20NDS%20%7C%20WASM%20%7C%20Python-lightgrey.svg)]()

**同じ入力、同じピクセル——ディスプレイなしの CI でアサート可能。**

Imprint は決定論的で埋め込み可能な C++17 UI ランタイムです：ピクセルバッファは 1 つ、ソフトウェアラスタライズ——GPU 不要、OS の GUI ツールキット不要。ホストがすべてのフレームを駆動するため、同じ入力シーケンスは常に同じフレームバイトを生みます。ヘッドレス CI で UI ロジックをピクセル単位にアサートできることは、テストハーネスの小技ではなく契約そのものの性質です。同じ UI ソースツリー——コード上のウィジェット、またはデザインファイルで記述した 1 画面——が、変更なしで Windows、Linux、macOS、WebAssembly、ニンテンドーDS、および `zbapi` 経由の任意の C ホストにコンパイルされます。

**デザインファースト。** 下のコンソールは *HTML で描いています*——ウィジェットコードなし——Imprint 自身のソフトウェアラスタライザが同じバッファにレンダリングします：

<p>
  <img src="assets/designs/imprint_console.png" width="860" alt="Imprint Console：真空管ダッシュボード（ tubes、VU バンク、電力メーター、診断パラグラフ）。HTML でデザインし、Imprint がレンダリング">
</p>

モックアップではなく、生きたウィジェットツリーです。デザイナーは HTML かコンパクトな `.ui` 形式を渡します。どちらも、あなたの C++ が作るのとまったく同じツリーに実体化するため、デザインが各ターゲットで実際に同梱されるものになります。HTML のパスは下の `.ui` サンプルの後に明記——いずれにせよ、1 つのツリー、1 つのバッファ、複数のターゲット。

**1 つの UI ソースツリー。1 つのピクセルバッファ。複数のターゲット。**

同じデザインファイルの showcase——ピクセルバッファに打ち抜かれた暖色のターミナル——がデスクトップ、ニンテンドーDS、ブラウザ上に：

![showcase_html は linux、nds、wasm 上に](assets/showcase/montage.png)

**[ブラウザでそのまま試す](https://tyouhyou.github.io/imprint/)** —— ページはウィジェット showcase の WebAssembly ビルドです。下のフレームはそのアプリのエンドツーエンド記録（デスクトップ・ブラウザ・DS ROM はいずれも同じソースからビルド）：

<img src="assets/showcase/showcase.gif" width="480" alt="フレームごとに記録した showcase：ダークで起動しチャートが描き出され、START でプログレスバーが充填、REPLAY でチャートを再生、ファクトリーコンソールのダッシュボード（ゲージ・ライブトレンド・セットポイントノブ・SELF-CHECK が PIXELS MATCH を刻印）、ライトの全ウィジェットページと影カード資産、ダークで締める">

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

- **決定論的・ホスト駆動のランタイム** — メインループはシェルが所有；同じ入力シーケンス → 同じピクセル；ダーティトラッキング付きオンデマンド再描画、隠れた再描画なし
- **契約による自動化** — スクリプトがユーザーの代わりを務められる：入力を与え、フレームをポンプし、ピクセルにアサート。シングルスレッドでタイマーなしのためドライバに sleep 不要——テストバッテリーには公開 API のみで駆動するエンドツーエンドの `automation` スイートを含む
- **デザインファイル** — `.ui` か外部 HTML で 1 画面を記述し、ビルド時に検証・パック。どのターゲットでも C 配列からロード。`ui_preview` がファイルを直接描画
- **保持モードのウィジェットツリー** — `Button`、`Label`、`Dialog`、`FlexPanel`、`GraphicsView` など
- **生のピクセルバッファへのソフトウェア描画** — GPU 不要、外部レンダリングライブラリ不要。バッファ形式はビルド時に `COLOR_DEPTH` で固定
- **C-ABI を第一級市民として** — 安定した `zbapi` C インターフェースに、Python（ctypes）、WebAssembly、C スモークテストのホスト
- **組み込みグレード** — RTTI なし、16 ビットカラー（abgr1555）、整数専用ジオメトリオプション、非アトミック参照カウントオプション（NDS に libatomic なし）
- **ゼロアロケーションのホットパス** — RAII の `ClipGuard`、イベントのトゥームストーン、`Subscription`
- **テキストは全体で UTF-8** — 組み込みの 5x7 ビットマップグリフフォールバック（ソース文字列から自動サブセット化）。ランタイム TTF テキスト（vendored stb_truetype）、vendored stb コーデック（PNG/JPEG）、手書き GIF ライターはオプション
- **C++17、CMake、静的ライブラリ** — すべて組み合わせ可能、強制されるものはなし

## 非目標

GPU 描画アクセラレーション（レンダリングカーネルは CPU ソフトウェアラスタライズのまま）·
アニメーション/トランジションシステム · 実行時バックエンド切替 · マルチスレッド描画 ·
IME 合成 · RTL レイアウト。Imprint は意図的に極小を保ちます：1 つのウィジェットツリー、
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

### HTML を外部デザイナーとして

本物のマークアップツールチェーンが良い？同じ実体化パスは外部 **HTML** デザインファイルも受け付けます。この README 冒頭のヒーローは
[`assets/designs/imprint_console.html`](assets/designs/imprint_console.html)
—— ブラウザで開いて編集し、Imprint のデザイナーに渡すと、上の C++ サンプルが作るのと同一のウィジェットツリーになります（`html` / `vectordial` サブセット：レイアウト、ラベル、コントロール、ベクターダイアル——Web エンジンではありません）。プレビューも同じ：

```
UI_PREVIEW_FILES="assets/designs/imprint_console.html" cmake -B build/build_html -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build/build_html
```

2 つの形式——`.ui` と HTML——は 1 つのツリー、1 つのピクセルバッファ、すべてのターゲットに供給します。

## ビルド

| ターゲット | コマンド | 備考 |
|---|---|---|
| Windows（MSVC） | `cmake -S . -B build/build_win && cmake --build build/build_win` | 依存ゼロのデフォルト（32bpp） |
| ランタイム TTF テキスト | `cmake -S . -B build/build_rt_ttf -DUSE_TTF_RUNTIME=ON && cmake --build build/build_rt_ttf` | ランタイム字形ラスタライズ（バッチ L-5）：アプリは `TtfFamily` でフォントを読み込み、外部依存なし |
| macOS（AppKit） | `cmake -S . -B build/build_mac && cmake --build build/build_mac` | deployment target の固定なし（ツールチェーン既定）、追加オプション不要 |
| Linux（X11） | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=X11 && cmake --build build/build_linux` | 入力対応バックエンド |
| Linux（フレームバッファ） | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux` | 表示のみ。操作は X11 で |
| ニンテンドーDS | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build/build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build/build_nds'` | `build/build_nds/bin/tictactoe.nds` を生成。`-DSTORY=showcase` でショーケース ROM をビルド（ホスト製の `ui_embed` と `asset_gen` を `-DUI_EMBED_EXECUTABLE=` / `-DASSET_GEN_EXECUTABLE=` で渡す必要あり） |
| WebAssembly | `demo/wasm/build.sh`（docker emscripten） | node スモークテスト付き |
| Python | `binding` 共有ライブラリをビルドしてから `SDL_VIDEODRIVER=dummy python3 demo/python/myapp.py --lib <libzbapi>` | ctypes + pygame ホスト |

テスト：`test/test_imui`——素の assert、テストフレームワークなし。デスクトップビルドで自動実行、NDS ではスキップ。

## ウィンドウと表示

アプリは**固定サイズのピクセルバッファ**（`create_window(w, h)`）を所有し、ウィンドウの
リサイズで再レイアウトすることはありません。デスクトップシェル（win32 / X11 / macOS）は
バッファサイズでウィンドウを開き、自由なリサイズを許可します。バッファはアスペクト比を
維持したまま黒のレターボックス中央にニアレストネイバーでスケーリング表示されます——同じ
バッファは同じウィンドウサイズですべてのデスクトッププラットフォームで同一に描画されます。
ポインタ入力はストレッチと同じ整数式（`buf = (win - dest) * buf / dest`）で逆マッピング
されるため、どのスケールでもヒット判定は正確です。レターボックス上のクリックは無視されます。
NDS とフレームバッファシェルは 1:1 表示、WASM/Python ホストはホスト側でスケーリングします。

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

**showcase**（`-DSTORY=showcase`）——マルチターゲットのウィジェットギャラリー。ダークで起動し、フレームワーク自身のラスタライザで描いたアニメーションチャート（角丸カード上のアンチエイリアス曲線＋グラデーション領域、app 側 tween が少しずつ描き出す）で開始。デバイス状態のコントロールパネル（プログレスバー、START/STOP、ダーク/ライトテーマ切替）と、アルファ資産合成付きの全ウィジェットページ（9-slice 影カード、アクセント色にティントしたボール。資産は `tools/asset_gen` がビルド時に生成）、ファクトリーコンソールのダッシュボードページ（ゲージ、ライブトレンドチャート、セットポイントのノブ＋スライダー、ポンプ/クーラントトグル）と SELF-CHECK ボタン——実際のドラッグでノブを操作し、同じ状態の 2 回のレンダリングがバイト単位で一致したとき PIXELS MATCH を刻印（決定論ランタイムの証明）。`assets/showcase/` のフレームはこれらのビルドから生成。レコーダーは完全に決定論的で、Windows/macOS/Linux でバイト単位で同一の GIF を生成します。WASM 版はオンラインで遊べます（[tyouhyou.github.io/imprint](https://tyouhyou.github.io/imprint/)、ローカルでは `demo/wasm/build.sh showcase`）。同じソースが NDS ROM もビルドします。

**三目並べ**（デフォルト story）——人間 vs コンピュータ。ダイアログ・ボタン・レイアウト・オンデマンド再描画を一通り使います。NDS ビルドは `build/build_nds/bin/tictactoe.nds` を生成します。3 つ目のアプリ `ui_preview`（`-DSTORY=ui_preview`）は `UI_PREVIEW_FILES`（スペース区切りのパス、左右キーでドキュメント切替）のデザインファイルを描画します——`.ui` か HTML のパスを渡せます。

| Windows | macOS | Linux (X11) | WebAssembly | ニンテンドーDS | Python ホスト |
|:---:|:---:|:---:|:---:|:---:|:---:|
| <img src="assets/tictactoe/win.png" width="240"> | <img src="assets/tictactoe/mac.png" width="240"> | <img src="assets/tictactoe/linux_x11.png" width="240"> | <img src="assets/tictactoe/wasm.png" width="160"> | <img src="assets/tictactoe/nds.png" width="200"> | <img src="assets/tictactoe/py256.png" width="240"> |

## ライセンス

[MIT](LICENSE) © 2026 tyou hyou
