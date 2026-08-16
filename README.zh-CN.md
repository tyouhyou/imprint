# Imprint

[![English](https://img.shields.io/badge/English-lightgrey)](README.md) [![中文](https://img.shields.io/badge/%E4%B8%AD%E6%96%87-blue)](README.zh-CN.md) [![日本語](https://img.shields.io/badge/%E6%97%A5%E6%9C%AC%E8%AA%9E-lightgrey)](README.ja.md)

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux%20%7C%20NDS%20%7C%20WASM%20%7C%20Python-lightgrey.svg)]()


**一个像素缓冲，跑遍所有目标。** Imprint 是一个零依赖、软件渲染的 C++17 UI 框架。同一份源码可以跑在任天堂 DS、Linux（framebuffer 或 X11）、Windows、浏览器（WebAssembly）以及通过 C-ABI 调用的 Python 宿主上。

| Windows | Linux (X11) | WebAssembly |
|:---:|:---:|:---:|
| <img src="assets/win.png" width="240"> | <img src="assets/linux_x11.png" width="240"> | <img src="assets/wasm.png" width="160"> |

| 任天堂 DS | Python 宿主 |
|:---:|:---:|
| <img src="assets/nds.png" width="240"> | <img src="assets/py256.png" width="240"> |

## 特性

- **保留模式控件树** — `Button`、`Label`、`Dialog`、`FlexPanel`、`GraphicsView` 等
- **设计文件** — 用极简文本格式（`.ui`）描述界面，构建期校验并打包成 C 数组，任何目标平台从数组加载；预览应用可直接渲染文件
- **软件渲染到原始像素缓冲** — 不需要 GPU，不需要外部渲染库；缓冲格式在构建期由 `COLOR_DEPTH` 固定
- **确定性的按需重绘** — 脏标记追踪，主循环归壳层所有，没有隐藏的重绘
- **C-ABI 一等公民** — 稳定的 `zbapi` C 接口，配 Python（ctypes）、WebAssembly 和 C 冒烟测试宿主
- **嵌入式级约束** — 无 RTTI、16 位色（abgr1555）、纯整数几何选项、非原子引用计数选项（NDS 没有 libatomic）
- **零分配热路径** — RAII `ClipGuard`、事件墓碑删除、`Subscription`
- **全链路 UTF-8 文本** — 内置 5x7 位图字形兜底（按源码字符串自动子集化）；可选 FreeType / libpng / libjpeg
- **C++17、CMake、静态库** — 一切可组合，不强加任何东西

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
UI_PREVIEW_FILES="tools/examples/menu.ui" cmake -B build_linux -DSTORY=ui_preview -DIM_SHELL_BACKEND=FB && cmake --build build_linux
```

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

测试：`test/test_imui.exe`（27 个套件，纯断言，无测试框架）。桌面构建自动运行；NDS 跳过。

## 示例

示例应用是井字棋（人机对战），覆盖对话框、按钮、布局与按需重绘。NDS 构建产出 `build_nds/bin/tictactoe.nds`。第二个应用 `ui_preview`（`-DSTORY=ui_preview`）渲染 `UI_PREVIEW_FILES`（空格分隔路径，左右键切换文档）指定的设计文件。

## 仓库结构

| 路径 | 内容 |
|---|---|
| `imcore/` | 绘制内核：Graphics / Color / Font / Image |
| `imui/` | 控件库（保留模式）+ 设计文件解析器 |
| `imapp/` | 应用接口：`IApp` / `IWindow` + 默认 `CanvasWindow` |
| `imevent/` | 事件与输入 |
| `imutil/` | 日志 |
| `binding/` | C-ABI `zbapi` 动态库 + C 冒烟测试 |
| `tools/` | 构建期工具：`ui_embed`（设计文件校验 + 打包）、字体子集化 |
| `imshell/` | 平台壳层（NDS / FB / X11 / Win） |
| `apps/` | 示例应用（`tictactoe`、`ui_preview`） |
| `test/` | 单元测试 |

## 许可证

[MIT](LICENSE) © 2026 tyou hyou
