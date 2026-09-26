# Imprint UI

> 本文件是英文版 README 的翻译，内容以 [README.md](README.md) 为准（更新至 2026-09-26）。

[![English](https://img.shields.io/badge/English-lightgrey)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-blue)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-lightgrey)](README.ja.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20macOS%20%7C%20NDS%20%7C%20WASM%20%7C%20Python-lightgrey.svg)]()

**相同的输入，相同的像素——可在无显示器的 CI 里断言。**

Imprint 是一个确定性、可嵌入的 C++17 UI 运行时：一个像素缓冲，纯软件光栅化——不需要 GPU，不需要操作系统 GUI 工具包。宿主驱动每一帧，因此同一输入序列永远得到同一帧字节；在无头 CI 里对 UI 逻辑做像素级断言，是契约本身的性质，不是测试脚手架的花招。同一份 UI 源码树——代码里的控件，或设计文件里描述的一屏——原样编译到 Windows、Linux、macOS、WebAssembly、任天堂 DS，以及经 `zbapi` 的任意 C 宿主。

**设计优先。** 下面这台控制台是*用 HTML 画的*——没有一行控件代码——由 Imprint 自己的软件光栅化器渲进同一个缓冲：

<p>
  <img src="assets/designs/imprint_console.png" width="860" alt="Imprint Console：电子管仪表盘（电子管、VU 表组、功率表、诊断段落），用 HTML 设计、由 Imprint 渲染">
</p>

不是效果图——是活的控件树。设计师交付 HTML 或紧凑的 `.ui` 格式；两者都物化成你的 C++ 所构建的那棵完全相同的树，设计即各目标上真正发布的界面。HTML 路径写在下面 `.ui` 示例之后；无论如何，一棵树、一个缓冲、多个目标。

**一份 UI 源码树。一个像素缓冲。多个目标。**

同一份设计文件 showcase——打进球缓冲里的暖色终端——在桌面、任天堂 DS 和浏览器上：

![showcase_html 在 linux、nds、wasm 上](assets/showcase/montage.png)

**[在浏览器里直接试](https://tyouhyou.github.io/imprint/)** —— 页面跑的是 widget showcase 的 WebAssembly 构建；下面这段是该应用的端到端录制（桌面、浏览器和 DS ROM 都由同一份源码构建）：

<img src="assets/showcase/showcase.gif" width="480" alt="逐帧录制的 showcase：暗色启动、图表逐步生长、START 填充进度条、REPLAY 重放图表、工厂控制台仪表盘（仪表、实时趋势、设定点旋钮、SELF-CHECK 打出 PIXELS MATCH）、浅色全控件页与阴影卡资产、回到暗色收尾">

不需要 GPU。不需要操作系统 GUI 工具包。不需要平台专属 UI 代码。

```
              同一份 UI 源码
                    │
        ┌───────────┼───────────┐
        ↓           ↓           ↓
     Windows      Linux       macOS
        │        (X11/FB)       │
        └───────────┼───────────┘
                    ↓
             WebAssembly  ←  浏览器直接试
                    ↓
               任天堂 DS
                    ↓
          你的嵌入式板子（C-ABI）
```

上例 `showcase` 应用的实测体积（Release 构建）：

| 目标 | UI 代码+数据 | RAM（静态） | 帧缓冲 | 交付体积 |
|---|---|---|---|---|
| 任天堂 DS | 543 KB text + 11 KB data | 7.7 KB BSS | 96 KB（256×192×2 B） | 646 KB `.nds` |
| WebAssembly | — | — | 256×192×4 B | 250 KB 单 `.js` 文件，`file://` 直开 |

## 特性

- **确定性、宿主驱动的运行时** — 主循环归壳层所有；相同输入序列 → 相同像素；脏标记追踪的按需重绘，没有隐藏的重绘
- **契约即自动化** — 脚本可以直接替代用户：喂输入、泵帧、对像素断言；单线程、无定时器，驱动方无需 sleep——测试集包含端到端 `automation` 套件，全程走公开 API
- **设计文件** — 用 `.ui` 或外部 HTML 描述一屏，构建期校验并打包，任何目标从 C 数组加载；`ui_preview` 直接渲染文件
- **保留模式控件树** — `Button`、`Label`、`Dialog`、`FlexPanel`、`ListBox` 等
- **软件渲染到原始像素缓冲** — 不需要 GPU，不需要外部渲染库；缓冲格式在构建期由 `COLOR_DEPTH` 固定
- **C-ABI 一等公民** — 稳定的 `zbapi` C 接口，配 Python（ctypes）、WebAssembly 和 C 冒烟测试宿主
- **嵌入式级约束** — 无 RTTI、16 位色（abgr1555）、纯整数几何选项、非原子引用计数选项（NDS 没有 libatomic）
- **零分配热路径** — RAII `ClipGuard`、事件墓碑删除、`Subscription`
- **全链路 UTF-8 文本** — 内置 5x7 位图字形兜底（按源码字符串自动子集化）；可选运行时 TTF 文本（vendored stb_truetype）、vendored stb 编解码器（PNG/JPEG）与手写 GIF 编码器
- **C++17、CMake、静态库** — 一切可组合，不强加任何东西

## 非目标

GPU 加速绘制（渲染内核保持 CPU 软件光栅）· 动画/过渡系统 · 运行时后端切换 ·
多线程渲染 · IME 组合 · RTL 排版。Imprint 刻意保持极小：一棵控件树、
一个像素缓冲、一路输入流——其余都是宿主的事。

## 快速示例

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
    btn->set_text("点我");
    btn->clicked += [] { printf("你好！\n"); };
    win->root().add_child(std::move(btn));

    app->paint();
}
```

同样的界面用设计文件描述（`tools/examples/menu.ui`）：

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

构建期用 `ui_embed` 打包（非法文件直接构建失败），运行时用
`parse_ui_text` + `build()` 物化 — 所有平台同一代码路径。预览交互式查看：

```
UI_PREVIEW_FILES="tools/examples/menu.ui" cmake -B build/build_linux -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux
```

### HTML 作为外部设计器

更想用真正的标记工具链？同一条物化路径也接受外部 **HTML** 设计文件。
本 README 顶部的 Hero 就是
[`assets/designs/imprint_console.html`](assets/designs/imprint_console.html)
——浏览器里打开即可编辑，交给 Imprint 的设计器，它会变成与上面 C++ 示例
完全相同的控件树（`html` / `vectordial` 子集：布局、标签、控件、矢量表盘——
不是 web 引擎）。预览方式相同：

```
UI_PREVIEW_FILES="assets/designs/imprint_console.html" cmake -B build/build_html -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build/build_html
```

两种格式——`.ui` 与 HTML——喂进同一棵树、同一个像素缓冲、每个目标。

### 以库的方式使用——你的工程、你的 `main`

以上示例组装的是仓库内 demo（`apps/` story + 顶层可执行文件）。你的应用
不必住进这棵树：把 Imprint 作为子工程加入，从你自己的 `main` 经
`zb::shell::run` 驱动——与平台 shell 跑的是同一个宿主循环：

```cpp
// 你的 main.cpp —— MyWindow : zb::app::CanvasWindow，或任意 zb::app::IApp
#include "shell/run.hpp"

#include "my_window.hpp"

int main()
{
    return zb::shell::run(std::make_shared<MyWindow>());
}
```

```cmake
# 你的 CMakeLists.txt
add_subdirectory(imprint)          # 或 FetchContent
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE imprint::imapp_canvas imprint::shell_backend)
```

作为子工程时 Imprint 只配置**库**——不含 demo 应用、binding、宿主工具；
`IMPRINT_WITH_TOOLS` / `IMPRINT_WITH_TESTS` / `IMPRINT_WITH_DEMOS` 逐项
开启（仓库内默认构建全部保留）。无头路径——CI 断言像素用的那条——完全
不需要 shell：`CanvasWindow::create()` + `paint()`（见
`test/external_smoke/`，已接入测试电池）。shell 循环契约见
`docs/code-contract.md` §11。

### 在 CI 里做确定性测试

同一输入序列永远产生相同的帧字节——所以 UI 逻辑可以逐像素断言，全程
无显示器。`zb::snap` 辅助库（链 `imprint::snapshot`）就是完整工作流：

```cpp
#include "snapshot.hpp"

// 无头驱动你的界面（input/paint），然后：
auto r = zb::snap::check(*app.window(), "main_view", "tests/baselines");
if (r.status == zb::snap::check_result::status::missing)
{
    zb::snap::record(*app.window(), "main_view", "tests/baselines");  // 首次运行
}
// status::mismatch 时还会生成 tests/baselines/main_view.actual.gif 供人工比对
```

把 `.zbsnap` 基线提交进仓库；CI 在任何像素漂移上失败，失配产物告诉
你改了什么。基线按构建配置生效（code-contract §12.2）；同一像素档内
hash 在 Windows / macOS / Linux 上字节级一致。仓库内参照：
`test/test_snapshot.cpp`（工作流）和 showcase 的 SELF-CHECK——它用的
就是同一个辅助库。

一行代码都没有也想出图？直接把设计文件渲染成像素——本 README 顶部的
Hero 就是这一行的产物（它同时打印帧 hash）：

```
imprint-render assets/designs/imprint_console.html --out hero.png
```

CI recipe——无浏览器、无显示器（我们的 Tier-1 job 跑的就是这段）：

```yaml
- run: |
    cmake --build build_ci --target imprint-render
    ./build_ci/bin/imprint-render tools/examples/menu.ui --out menu1.png
    ./build_ci/bin/imprint-render tools/examples/menu.ui --out menu2.png
    cmp menu1.png menu2.png   # 两次运行，帧字节级一致
```

### 脚本语言的 GUI——设计文件 + 回调

声明式路径让用户侧完全不需要 C++：`zb_app_create_from_ui` 从设计文件
（`.ui` 语法，或 `is_html=1` 走 HTML 子集——与设计器相同的两个前端）
构建控件树，动作按 id 回来。Python demo 就是完整故事
（`demo/python/ui_app.py`）：

```python
UI = """
column id="root" spacing=8 padding=12
  label id="count" text="Clicks: 0"
  button id="inc" text="Count up"
"""

def on_action(widget_id, userdata):
    if widget_id == b"inc":
        clicks[0] += 1
        lib.zb_widget_set_text(app, b"count", ("Clicks: %d" % clicks[0]).encode())

app = lib.zb_app_create_from_ui(UI.encode(), 0, 320, 240)
lib.zb_set_event_callback(app, b"inc", action_cb, None)
# 在你自己的循环里驱动 zb_input / zb_paint —— 宿主就是 shell
```

任何能调 C ABI 的语言拿到的是同一套协议。静态结构住在文件里，行为住在
宿主里——声明式边界不变。

## 构建

| 目标 | 命令 | 说明 |
|---|---|---|
| Windows（MSVC） | `cmake -S . -B build/build_win && cmake --build build/build_win` | 零依赖默认构建（32bpp） |
| 运行时 TTF 文本 | `cmake -S . -B build/build_rt_ttf -DUSE_TTF_RUNTIME=ON && cmake --build build/build_rt_ttf` | 运行时字形栅格化（批次 L-5）：应用经 `TtfFamily` 加载字体，无外部依赖 |
| macOS（AppKit） | `cmake -S . -B build/build_mac && cmake --build build/build_mac` | 不钉 deployment target（工具链默认），无需额外选项 |
| Linux（X11） | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=X11 && cmake --build build/build_linux` | 支持输入的后端 |
| Linux（framebuffer） | `cmake -S . -B build/build_linux -DIM_SHELL_BACKEND=FB && cmake --build build/build_linux` | 仅显示；交互请用 X11 |
| 任天堂 DS | `docker run --rm -v $PWD:/src -w /src devkitpro/devkitarm:20260610 sh -c 'cmake -S . -B build/build_nds -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake && cmake --build build/build_nds'` | 产出 `build/build_nds/bin/tictactoe.nds`；加 `-DSTORY=showcase` 构建 showcase ROM（还需传入宿主构建的 `ui_embed` 与 `asset_gen`：`-DUI_EMBED_EXECUTABLE=` / `-DASSET_GEN_EXECUTABLE=`），或 `-DSTORY=showcase_html` 构建 HTML showcase（同上，另需宿主构建的 `html_embed`：`-DHTML_EMBED_EXECUTABLE=`） |
| WebAssembly | `demo/wasm/build.sh`（docker emscripten） | 附带 node 冒烟测试 |
| Python | 先构建 `binding` 动态库，再 `SDL_VIDEODRIVER=dummy python3 demo/python/myapp.py --lib <libzbapi>` | ctypes + pygame 宿主 |

测试：`test/test_imui`——纯断言，无测试框架；桌面用 `ctest -R test_imui`（或直接运行二进制），NDS 跳过。

## 窗口与呈现

应用拥有**固定尺寸的像素缓冲**（`create_window(w, h)`），窗口缩放从不触发重新布局。桌面壳
（win32 / X11 / macOS）以缓冲尺寸打开窗口并允许自由缩放：缓冲按等比缩放居中呈现（黑色
letterbox 留边），最近邻重采样——同一缓冲在同一窗口尺寸下于所有桌面平台渲染一致。指针输入
经与正向拉伸同一整数公式（`buf = (win - dest) * buf / dest`）逆映射，任意缩放命中测试都精确；
letterbox 上的点击被忽略。NDS 与 framebuffer 壳按 1:1 呈现；WASM/Python 宿主在宿主侧缩放。

## 文档

**建议阅读顺序**（新维护者的第一遍）：
1. [`docs/getting-started.md`](docs/getting-started.md)——跑起第一个应用并改成自己的（约 5 分钟）
2. 本 README → **构建**（在各目标上把二进制跑起来）
3. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §1–§2——系统是什么、模块图与依赖规则
4. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) §3–§5——规范性契约、目标平台、已知限制
5. [`docs/code-contract.md`](docs/code-contract.md)——API 级接口契约
6. [`docs/design-file.md`](docs/design-file.md)——处理 `.ui` 文件时再读

**按任务找文档**：改公开 API → 先改 `code-contract.md`（契约先于 API）· 新目标 / 新像素格式 / 新构建选项 → `docs/backlog.md` 与 ARCHITECTURE §4 · `.ui` 语法或打包 → `design-file.md` · C-ABI 宿主 → `zbapi.h` + ARCHITECTURE §4.8 · 构建/运行命令 → 下方**构建**。

- [`docs/getting-started.md`](docs/getting-started.md)——从克隆到跑起自己的应用：运行 `hello` 示例、理解 `IApp`/`CanvasWindow` 接缝、注册自己的 story
- [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)——按现状实装的架构：模块图与依赖规则、契约（帧生命周期、输入、像素模型、文本、事件、错误处理、C-ABI 宿主、构建选项）、已知限制
- [`docs/backlog.md`](docs/backlog.md)——活跃 Backlog：架构项、产品特性批次（L/I/F）、条件触发项
- [`docs/code-contract.md`](docs/code-contract.md)——API 级接口契约：错误路径、UTF-8/文本、glyph provider、树变更、布局失效、分配预算、呈现接缝转换器
- [`docs/design-file.md`](docs/design-file.md)——`.ui` 设计文件格式：语法、打包管线、物化语义
- [`binding/include/zbapi.h`](binding/include/zbapi.h)——C-ABI 宿主接口；宿主规则见 ARCHITECTURE §4.8

## 示例

**Hello**（`-DSTORY=hello`）——入门应用：一个标签加一个点击计数的按钮；复制它即可开始写自己的应用（见 [`docs/getting-started.md`](docs/getting-started.md)）。

**showcase**（`-DSTORY=showcase`）——多目标控件陈列馆：暗色启动，开场是一幅用框架自身光栅器绘制的动画图表（圆角卡片上的抗锯齿曲线 + 渐变面积，由 app 侧 tween 逐步揭示）；设备状态控制面板（进度条、START/STOP、深/浅主题切换）与全控件页面带 alpha 资产合成（9-slice 阴影卡、accent 染色球；资产由 `tools/asset_gen` 构建期生成），以及工厂控制台仪表盘页（仪表、实时趋势图、设定点旋钮+滑块对、泵/冷却 toggle），带 SELF-CHECK 按钮——用真实拖拽驱动旋钮，同一状态两次渲染的帧缓冲逐字节哈希一致时打出 PIXELS MATCH（确定性运行时的证明）。`assets/showcase/` 中的画面来自这些构建——录制器完全确定，Windows/macOS/Linux 上产出字节级一致的 GIF；WASM 变体可在线游玩（[tyouhyou.github.io/imprint](https://tyouhyou.github.io/imprint/)，本地用 `demo/wasm/build.sh showcase` 构建），同一份源码也构建 NDS ROM。

**井字棋**（默认 story）——人机对战，覆盖对话框、按钮、布局与按需重绘；NDS 构建产出 `build/build_nds/bin/tictactoe.nds`。第三个应用 `ui_preview`（`-DSTORY=ui_preview`）渲染 `UI_PREVIEW_FILES`（空格分隔路径，左右键切换文档）指定的设计文件——可传 `.ui` 或 HTML 路径。

| Windows | macOS | Linux (X11) | WebAssembly | 任天堂 DS | Python 宿主 |
|:---:|:---:|:---:|:---:|:---:|:---:|
| <img src="assets/tictactoe/win.png" width="240"> | <img src="assets/tictactoe/mac.png" width="240"> | <img src="assets/tictactoe/linux_x11.png" width="240"> | <img src="assets/tictactoe/wasm.png" width="160"> | <img src="assets/tictactoe/nds.png" width="200"> | <img src="assets/tictactoe/py256.png" width="240"> |

## 许可证

[MIT](LICENSE) © 2026 tyou hyou
