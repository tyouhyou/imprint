# studio — 创作会话 C++ 客户端（imprint / SIXEL）

> P1-10（天机仓 backlog）：五窗格创作会话屏的 imprint 终端壳。
> **界面复刻 Textual 原型机（天机仓 `studio/ui.py` 的 `SessionApp`），
> 功能零复制** —— 一切命令走 `studio-bridge` 子进程（天机仓
> `docs/09` §9.1 线协议），本层只做投影与路由。

```
┌─ 天机剧场 · Studio · 创作会话 ────────────────── 作品 … ─┐
│ 会话历史（最近）            │ 文本窗口 · manuscript.md   │
│ 🔒/锁 标记 + 会话列表        │ + AI 待签收草稿（只读）      │
│────────────────────────────│                            │
│ 对话窗口                    │                            │
│ 你 ▸ … / ai.writer ▸ …      │                            │
├─────────────────────────────────────────────────────────┤
│ 输出（提示 · 回执 · 错误 · help）                          │
├─────────────────────────────────────────────────────────┤
│ 状态行（⏳=〔AI 回合中〕）                                 │
│ 非 / 文本=发言  [输入行]                                  │
└─────────────────────────────────────────────────────────┘
```

## 构建

```bash
cd imprint
cmake -S . -B build/build_studio -DSTORY=studio \
      -DIM_SHELL_BACKEND=SIXEL -DUSE_TTF_RUNTIME=ON
cmake --build build/build_studio -j
# 协议单测（三套件，含 mock 假桥 spawn 回环）
./build/build_studio/bin/test_studio
```

## 运行

```bash
TIANJI_HOME=/path/to/tianji \
TIANJI_FONT=/usr/share/fonts/truetype/wqy-zenhei.ttc \
IM_TERM_SIZE=1200x800 \
./build/build_studio/bin/studio
```

| 环境变量 | 默认 | 语义 |
|---|---|---|
| `TIANJI_HOME` | `../tianji` | 天机仓根：桥子进程的 cwd 与 `--root`（`sessions/`、`works/` 在此） |
| `STUDIO_BRIDGE_CMD` | `.venv/bin/python -m studio.pipeline studio-bridge --root .` | 桥命令（`sh -c` 执行）；冒烟用 `studio/bridge/mock_bridge.py` 覆盖 |
| `TIANJI_FONT` | （无 → 5x7 降级） | 运行时 CJK TTF（wqy-zenhei 为 .ttc，stbtt 取 offset 0）；缓存 2MB 对话档 |
| `IM_TERM_SIZE` | 320x240 | SIXEL 缓冲像素（建议 1200x800；帧尺寸进程内不可变） |

## 键位（SIXEL 终端适配，FI-11）

终端输入解析器尚不解析 Shift/Ctrl+方向键和弦，TUI 原型的键位表适配为
Ctrl 字母和弦（FI-11 落地后一行切回）：

| 动作 | 键 | TUI 原型 |
|---|---|---|
| 退出 | `Ctrl+Q` | 同 |
| 对话窗滚 ↑/↓ | `Ctrl+E` / `Ctrl+Y` | Shift+↑/↓ |
| 输出窗滚 ↑/↓ | `Ctrl+W` / `Ctrl+S` | Ctrl+↑/↓ |
| 任意窗格滚动 | 鼠标滚轮 | 同 |
| 历史会话打开 | 点击行（= 发 `/open <id>`） | 同 |
| 文本窗浏览 | 点击聚焦后方向键/滚轮 | 同 |

回合期间输入行只读（桥是 busy 权威：快照 `busy` 位 + `turn.finished`
事件解锁，docs/09 §7.5 一回合一事）。

## 呈现差异（有意为之，非缺陷）

- ListBox 行不携样式 → TUI 的配色改用**文本前缀**：席位标签
  （`你 ▸` / `ai.writer ▸` / `·系统·`）与草稿指针 `〔稿〕`；
  行内 CJK 经 FI-12 树内修复（`make_text_image` provider/左对齐重载）
  由运行时字体渲染；
- 字形覆盖取舍：历史列表 🔒/🔓 用 `锁`/`开`、当前标记 ▸ 用 `>`
  （zenhei 无此等字形，缺字直接跳空）；状态行 🔒 emoji 同理省略；
- 回执行前缀（✅/⚠/❌）保留桥原文，字体缺字形时整字跳过、文字可读。

## 文件地图

| 文件 | 作用 |
|---|---|
| `bridge/bridge_client.{hpp,cpp}` | 桥客户端：fork/exec 两管道、非阻塞 poll、id 关联请求、事件队列；`attach()` 测试缝 |
| `bridge/json.hpp/.cpp` | 线 DOM——**不复制**：tianji/CMakeLists 拆出的 `tj_json` 公共目标（命名空间仍是 `zb::tianji`） |
| `bridge/mock_bridge.py` | stdlib-only 假桥：单测/pty 冒烟不需要天机仓在场 |
| `app/studio_app.{hpp,cpp}` | 五窗格 CanvasWindow：投影 + App 级键位 + step 轮询（fp 指纹 ≈1s，同 TUI 1s 节拍） |
| `app/app_maker.cpp` | `zb::app::make_app()` 工厂（A-22 组合点） |
| `test/test_studio_bridge.cpp` | 协议单测（入 imprint 测试电池） |
| `test/smoke_pty.sh` | pty 冒烟（真终端形状，假桥驱动，结构断言非逐字节） |

## 边界（硬约束的客户端侧读法）

- 客户端不发明命令：一切输入 = `console.input` passthrough，治理边界
  仍是天机仓的命令表（docs/10 §1）；
- 线上只传结构化数据：席位标签/前缀等呈现规则在本层，镜像 TUI 投影
  代码（studio/ui.py）；
- 桥死 = 只读状态（输出窗一行告警），Ctrl+Q 关闭——重启即重 spawn。
