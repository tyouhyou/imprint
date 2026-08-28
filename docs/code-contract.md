# 代码契约（Code Contract）

本文件是框架的公开 API 接口契约。实现与文档冲突时以本文件为准；
改接口形态前必须修订本文件。

分工（单方向依存）：架构级事实——模块边界、渲染循环、输入管线、
像素模型、事件系统、C-ABI 宿主规则、构建矩阵——**不在本文重复**，
一律以 `docs/ARCHITECTURE.md` 为准；设计文件 `.ui` 的语法与物化语义
以 `docs/design-file.md` 为准。本文只描述它们之上的 **API 级契约**：
错误处理、文本/Unicode、字形 provider、树变更、布局失效、分配预算。

---

## 1. 错误处理规范

### 1.1 两条路径，一个原则

框架 API 按调用频率和失败语义分成两条路径，新 API 设计时**先归路径
再写实现**：

| 路径 | 判定 | 失败信号 | 禁止 |
|---|---|---|---|
| 热路径 | 每帧 / 每次输入派发可到达（渲染、命中测试、dispatch、属性设置） | 返回值：`nullptr` / `bool` / 显式 out 参数 | 抛异常、abort、整数错误码 |
| 初始化路径 | 一次性：构造、资源/文件/字体加载 | 抛 `zb::ui::error`（携带 msg 的 `std::exception` 子类） | 静默吞错、裸返回整数错误码 |

理由：异常在 NDS 等嵌入式目标上代价高，部分工具链默认关闭异常；
而初始化路径的错误（字体文件缺失、资源损坏）必须带上下文信息，
`error` 的 msg 是最廉价的传递方式。

### 1.2 命名约定

- `_safe` 后缀 = "不抛异常"的强制约定：提供 `foo()`（初始化/内部断言
  语义）与 `foo_safe()`（热路径语义）时，`_safe` 变体必须是字面意义上
  never throws。
- 只提供一种形态时，热路径接口**不加**后缀（默认就是不抛），例如
  `Widget::set_text`。

### 1.3 现状清单

| 位置 | 现状 | 判定 |
|---|---|---|
| `Graphics::clip_safe` | RAII `ClipGuard`（栈值，零分配）：保存/恢复绘制状态，never throws，widget 绘制路径已使用 | ✓ 合规 |
| `Graphics::clip` | 已删除（无调用点，抛异常语义被 ClipGuard 取代） | ✓ |
| `Graphics::clone` | 保留，deep-copy 一次性用途；无热路径调用点 | ✓ 合规 |
| `Font` 构造 | 抛 `Font::error`（RAII 防泄漏已修） | ✓ 合规（初始化路径） |
| `Graphics` 构造 / `clone` | 抛 `zb::ui::error`：构造对零尺寸与像素数超 int（64 位乘法防回绕，65536×65536 曾回绕为 0 分配空缓冲仍报全尺寸 draw_area → 首次 fill/draw 越界写）；clone 的越界守卫用 64 位和（`x+width` 曾回绕为负通过检查 → 越界读） | ✓ 合规（初始化路径） |
| `codec/`（png/jpeg，vendored stb 实现） | `int` 错误码，0=OK；读路径：1=打开失败 2=非该格式 3=解码失败 4=零尺寸 5=info 回调拒收 6=行回调拒收；写路径：1=零尺寸 2=行回调拒收 3=打开失败 4=写出失败。2026-08 换 stb 时重排过一次（原 libpng/libjpeg 编号 1~5/-1），语义不变、号码不作长期承诺 | 边界内契约：文件 I/O 可能运行在禁用异常的环境，错误码在此边界**保留**；对外 wrapper（未来）再按初始化路径抛 `zb::ui::error` |
| `fb.cpp` TODO("throw error") | 未实现 | 保持 TODO，禁止改成热路径抛异常 |

### 1.4 `zb::ui::error` 形态

```cpp
class error : public std::exception
{
public:
    explicit error(const std::string &msg) noexcept;
    const char *what() const noexcept override;
private:
    std::string msg;
};
```

`Font::error` 是现状同名类型，未来收敛为公共 `zb::ui::error` 的别名
或直接替换；`zb::ui::error` 本体已落地在 `imcore/include/core/error.hpp`
（`what() const noexcept`、消息按 `const&` 收），在此之前的 `Font::error`
即满足"带 msg 的异常"要求。

### 1.5 Event 回调契约

- `Event::invoke` / `operator()` 是热路径（输入回调、状态通知），
  **handler 不得抛异常**：异常逃逸出 handler 属于违反契约。理由见
  §1.1——异常在 C-ABI 宿主路径上会被边界 `catch(...)` 吞掉（wasm 上
  则是 trap），若此时事件内部状态已损坏，程序会在"看起来还活着"的
  状态下继续运行，handler 表永久泄漏。
- 防御性保证：invoke 深度由 RAII guard 维护（`imevent/event.hpp`
  `InvokeGuard`），即使 handler 违规抛出，深度也正确回卷、最外层
  invoke 的 tombstone compaction 照常执行——`unsub` 的"invoke 外
  立即 erase、invoke 内墓碑"语义在任何路径下成立，一次异常不会给
  handler 表留下永久墓碑。
- 回调错误一律走返回值 / out 参数 / 日志（`LW/LE`）汇报，禁止用
  异常做控制流（与 §1.1 热路径规定一致）。
- `CanvasWindow::input` 标记 `noexcept`：应用/控件回调从中抛出 =
  `std::terminate`——帧状态不可预期时带病续跑比快死更危险，宿主不得
  依赖捕获。

---

## 2. 文本 / Unicode 接口决策

### 2.1 内部表示：`std::u16string`（现状，维持）

- `Widget` 文本存储为 `std::u16string`（`widget.hpp`）。
- `Font::measure` / `Font::write` 接受 `const char16_t*`（FreeType 的
  UTF-16 语义），widget 绘制直接传内部缓冲，无拷贝转换。

### 2.2 输入形态：框架 API 一律 UTF-8

- `const char*` 入参语义**一律定义为 UTF-8**，没有例外（`set_text`、
  `make_text_image`、未来所有文本入口）。
- 理由：平台无关、无 wchar 宽度/ABI 争议、与 C-ABI（zbapi）直通、
  Linux/NDS 原生态；Win32 壳负责 wchar↔UTF-8 的转换，`wchar_t`
  永不进入框架 API（Win32 `TEXT()`/MBCS 宽度问题的根治）。
- **已落地**：`set_text(const char*)` 经 `utf8_to_utf16`
  （`imcore/text/utf8.hpp`）解码；签名不变。

### 2.3 转换层位置

- UTF-8 ↔ UTF-16 转换器位于 `imcore/text/utf8.hpp`（已落地：
  `utf8_to_utf16` / `utf16_to_utf8`），与 `Font` 同层。
- `imcore` 层不依赖 `imui`；widget 层转换只是"解码 → 追加到 u16 缓冲"。

### 2.4 双字形 provider（已落地）

`GlyphProvider` 抽象 + `BitmapProvider`（内置 5x7，永不依赖
FreeType）+ FreeType 包装 provider 已落地于 `imcore/include/text/`；
`Widget::set_glyph_provider()` 安装主 provider，`set_font` 保持为
便捷别名。存续的契约义务：

- 降级链：主 provider 返回"未覆盖字形" → 兜底 provider → 仍缺则跳过。
- `#if defined(USE_FONT)` 条件编译只允许存在于 provider 选择处，
  widget 绘制路径无条件化。
- 不改变 `set_text` / 内部 u16 缓冲的形态；`make_text_image` 保持独立
  API（tictactoe 依赖，位图字体直绘，不走 widget 文本路径）。

**字体子集（批次 E 定稿，2026-08-15；A-7 补 2026-08-28）**：BitmapProvider 的 5x7 表
为两段：内置 ASCII 32..95（`kGlyphs`）+ 构建期生成的子集表
（`subset_glyphs.hpp`，仅当 `IMCORE_HAS_SUBSET` 定义时编译，由
`imcore/CMakeLists.txt` 在 configure 期调用 `tools/font_subset.py`
扫描 `apps/` 与 `test/` 源码字符串字面量、与 `tools/extra_glyphs.py`
手绘字形表求交后生成，按码元排序供二分查找）。契约：
- 源码用到的码元必须在 `extra_glyphs.py` 手绘 5x7 字形，漏画只出
  构建警告（不失败）：该字符运行时零宽跳过——"运行时生成的动态
  文本不在子集内"同此语义，不在静态源码中的字符不会被纳入表。
- **设计文件来源（A-7）**：`.ui` 文档中的 `text="…"` / `items="…"`
  同属源码输入，必须纳入扫描（`imcore/CMakeLists.txt` 追加 `*.ui`
  glob，`font_subset.py` 增加 `.ui` 解析分支）；未纳入前，`.ui`
  中的非 ASCII 文本运行时零宽跳过为已知缺口。
- 无 Python 3 或无 `FONT_SUBSET=ON` 的构建回退为 ASCII-only：
  `IMCORE_HAS_SUBSET` 未定义，覆盖集仅为 32..95，行为与批次 E 前
  完全一致（测试用 `#if defined(IMCORE_HAS_SUBSET)` 双分支断言）。
- 5x7 画 CJK 不可读（至少 12x12）：CJK 走方案 2（stb_truetype 构建
  期 TTF→位图层转换器），不在批次 E 方案 1 的承诺内。

---

## 3. 其他 API 形态守则

架构级背景（POD 输入事件、无 RTTI 遍历、C-ABI 宿主规则、渲染循环）
见 `docs/ARCHITECTURE.md` §4；本节只保留 API 层面的补充契约。

- 关闭通知契约：宿主责任与重入禁令见 ARCHITECTURE.md
  §4.8；API 层补充——wasm 宿主经 `addFunction` 注册回调（构建需
  `ALLOW_TABLE_GROWTH=1`）。

### 3.1 字符事件契约

- `input_event.ch`（int，0=无字符）是 key_down/key_up 携带的**可打印字符**
  通道。当前语义 = ASCII 码点（0x20~0x7e）；契约已按 Unicode 码点写好，
  未来扩展为 UTF-32 值时 `key`/控件内 u16 转换层同步跟进，API 签名不变。
- 语义划分：`key` 承载导航/编辑键（`key_code`：tab/enter/space/arrows/
  backspace/del/escape），`ch` 承载字符。两字段独立：导航键只设 key
  （ch=0）；产生文本的键可同时设两者（空格例外——shell 约定空格走 key
  保持焦点激活语义，TextInput 依 key==space 插入；其余可打印字符走 ch）。
- 路由优先序：dispatcher 对 ch!=0 的按键**永不参与焦点导航**；先送焦点
  控件 `on_input`，未消费即丢弃。
- 宿主责任：key→ch 的字符映射在各壳层完成（x11 XLookupString / win
  WM_CHAR / js `e.key` / pygame unicode），框架内不做 key→ch 推导。
- C-ABI 形态：`zb_input(app, type, x, y, key, ch, touch_id)`；
  非键盘事件 ch 恒传 0。
- key_up 现状：dispatcher 只派发 key_down（`handle_key` 对
  key_up 返回 false），桌面壳亦不转发释放事件；控件需要 release 语义
  前须先扩本契约与壳层。
- 公开头文件自包含、可被 C 宿主 include 的边界文件不泄漏 C++ 类型。
- 控件尺寸：`Widget::measure()` 返回自然尺寸（默认 = 当前 size；Label/Checkbox/RadioButton/Slider/ListBox 有内容派生覆写）。`set_size` 置 explicit 标记，布局层（FlexPanel）只对非 explicit 子项套用 measure()；`set_size_auto` 供布局内部回写尺寸并清除 explicit，应用代码不直接用。explicit 尺寸永远优先于 measure()。
- **explicit 按轴记录**：`size_explicit_w_/h_` 两轴独立；`is_size_explicit()` = 任一轴（兼容语义），`is_width_explicit()/is_height_explicit()` 按轴查询。FlexPanel 的份额分配与尺寸落位用 `set_width_auto()/set_height_auto()` **按轴回写、只清该轴标志**——显式交叉轴尺寸在主轴 grow 时保留其值与标志。
- 键盘约束：modal 打开时，键盘焦点与 Tab/方向键导航限定在 modal 子树（`focus_next` 以 modal 为 scope）；焦点控件位于 modal 之外（modal 打开前聚焦）或已不可见（`is_effectively_visible()` = 自身与全部祖先可见）时，下一次按键前释放焦点。按压中目标被隐藏时，指针事件先投递 `on_cancel` 再清除按压，后续 move/release 不再送达。
- wheel 通道：`ev.delta` 的单位是**带符号 notch**（滚轮一格 = ±1），
  壳层在派发前归一（win = `GET_WHEEL_DELTA_WPARAM / WHEEL_DELTA`，
  自由滚轮的亚 notch 增量丢弃；x11 = button4/5 → ±1）——控件可以
  依赖量级而不只是符号。C-ABI `zb_input` 的 `key` 参数对 mouse_wheel
  承载该 delta（zbapi.h 既有契约），`zbapi.cpp` 映射进 `ev.delta`；
  `ev.x/y` 必须是**指针的客户区坐标**（dispatcher 按坐标选目标）。

## 4. 描述式 UI builder（批次 G 契约）

- `ui_node` 是静态描述的唯一入口：type/id/有序 props/children/items/flex_grow。fluent builder（column/row/panel/label/button/checkbox/radio/slider/list_box/text_input + .size/.pos/.text/.named/.checked/.group/.step/.rows/.spacing/.padding/.wrap/.flex/.visible）与未来设计文件反序列化器（G6）共享同一中间表示——二者产生的 props 必须能被同一张属性解析表消费。
- `build(host, root)`：root 节点自身是文档（host 才是真实容器，root tag 不实例化控件）；root 的 spacing/padding/wrap 属性在 host 为 FlexPanel 时生效，spacing/padding 在 Panel 时生效；root 的 children 逐个 materialize 进 host。
- materialize 语义：未知 tag 打日志跳过（LW）；非容器 tag 带 children → children 静默丢弃（LW）；host 是 FlexPanel 时子节点带 flex_grow 走 flex 布局，否则按 Panel 线性布局。
- 属性解析容错：属性缺失或类型不匹配 → 使用默认值（静默，不抛异常，init 路径无 throw 原则适用）；`text` 属性以 UTF-8 字符串接受（内部转 u16）。
- 无 RTTI 约束：materialize 的 static_cast 合法，因为属性解析表只在工厂表创建的控件上运行——tag 表与 apply 分支必须保持一致，改动任何一侧都要同步另一侧（两处均在 ui_builder.cpp）。
- 契约边界（不可进描述层）：动态 model（ListBox 的 ItemText 函数指针）、事件订阅（Event<> 接线）、字体/字形内容、运行时生成的文本。描述层只承载静态结构 + 属性 + id。
- id 引用：`Widget::find_by_id` 深搜子树返回首个匹配（线性查找，仅用于接线/调试，不进热路径）；未命中返回 nullptr。事件绑定方式 = materialize 后按 id 取 Widget* 再走 Event<> 订阅（框架不引入回调注册表）。

## 5. 设计文件（批次 G6 定稿）

### 5.1 形态决策（用户拍板，勿再翻转）

- **自研极简文本格式（非 XML）**：单一 C++ 解析器进 `imui`（`parse_ui_text`），
  吃内存字节串，来源解耦（桌面=文件，NDS=嵌入 C 数组，将来热更新只换来源）。
- **不做 uic 式翻译**：不把文本转成 C++ 构建代码（多宿主共享代码路径、WASM/Python
  用不上、双表同步负担）。
- **统一打包嵌入**：所有平台构建期把 `.ui` 文本转 C 数组（`tools/ui_embed`），
  UI 定义不随二进制分发、用户不可改；预览器是唯一保留读外部文件的例外。

### 5.2 文本格式

语法规范（节点/key=value、缩进嵌套、续行、注释、容错语义）、根节点
处理与物化语义的**唯一权威定义**：`docs/design-file.md`。解析器实现
（`imui/src/ui_file.cpp`）必须与其保持一致；改语法先改该文档。

### 5.3 打包契约

- `parse_ui_text(text, ok)` 是唯一解析入口：返回 `ui_node`（与 fluent builder
  共享中间表示，见 §4）；`ok=false` 当文档无节点（解析是容错路径，不 throw）。
- `ui_embed <out_header> <file.ui>...`：构建期打包器，零运行时依赖；pass1 用
  库解析器校验每个文档（失败 exit 1 = 构建错误），pass2 生成
  `embedded_ui_file{name,bytes,len}` + `kUiFiles[]` + `find_ui_file(name)`。
  生成的字节数组在文档字节之后带一个 `0x00` 哨兵（`parse_ui_text` 按 NUL 终止
  扫描，与 pass1 校验时的输入形状一致）；`size` 记文档字节数、不含哨兵，
  消费方可以依赖 `data[size] == 0`。
- 消费方式：`find_ui_file` 取字节 → `parse_ui_text` → `build(host, doc)`；
  所有平台走同一代码路径。

---

## 6. 树变更协议（批次 J3 定稿）

动态增删控件的安全契约，防 dispatcher 裸指针悬空与 UAF：

- **移除必经协调入口**：对见过输入的树，删除子控件必须经
  `CanvasWindow::remove_from(panel, widget)`（或 `clear_root_children()`）；
  对 tree 级 root 使用 `root_->remove_child(w)`。
- **直接调用 `Panel::remove_child` / `FlexPanel::remove_child` 是
  协调-free 路径**：仅当调用方保证该子树从未参与输入派发（无
  pressed/focus/modal 指针指向它）时可用；否则必须先
  `InputDispatcher::evict(widget)`。
- `evict` 清理三指针：pressed_target（活动按压先投递 on_cancel 再
  置空）、focus_target（释放焦点）、modal（含 modal 子树内的情况）。
- `remove_child` 返回 `std::unique_ptr<Widget>`（所有权转移给调用方，
  找不到返回 nullptr），**并把 `w->parent` 重置为 nullptr**；
  `clear_children()` 全部摘除后销毁。
- 移除后 `find_by_id` 不再命中；重复 remove 同一指针返回 nullptr。
- 被移除节点持有的 Event 订阅随析构安全退订。

## 7. 布局协议（批次 J5 定稿）

- `layout_dirty_` 是布局失效标志：几何/内容 setter（set_size、
  set_size_auto、set_text、set_font、set_glyph_provider；容器
  add_child/remove_child、set_orientation/set_spacing/set_padding）
  `mark_layout_dirty()` 沿 parent 链上溯到 root（零分配）。新树初始
  dirty（`layout_dirty_ = true`）。布局结束清自身标志，故同一状态最多
  触发一次布局。
- **自动布局是宿主显式开启的门控行为**（`CanvasWindow::set_auto_layout(true)`，
  默认关）：开启后 `paint()` 在 damage walk 前执行
  `if (root_->is_layout_dirty()) root_->layout();`。默认关闭时现行为
  （显式 set_position/set_size 原样保留、手动 layout 幂等）完全不变。
  UI 描述宿主（ui_preview）必须显式开启。
- **paint 内顺序（layout → damage → draw）的架构定义见
  ARCHITECTURE.md §4.1**；API 义务：布局内部触发的 mark_dirty 必须被
  随后的 damage walk 收走，geometry 与绘制同帧一致。
- 失效传播只置位布局标志、不额外标记 render dirty（几何变化由布局
  回写自身 setter 自然产生 damage）。
- text advance 缓存（批次 J4）失效义务：任何改变字形内容的 setter
  （set_text/set_font/set_glyph_provider）必须重置缓存；新增此类
  setter 时必须同步失效 + 更新本节 + 闸门测试兜底。
- intrinsic-setter 审计义务：measure() 覆写控件的"喂给
  measure() 的参数"setter（checkbox 的 box_size/text_gap、radio 的
  circle_size/text_gap、list_box 的 row_height——经 set_size 重算派生
  高度顺带失效）必须 `mark_layout_dirty()`；新增此类 setter 同 §7
  失效义务处理。

## 8. 分配预算（批次 J7 定稿）

- **热路径定义**：每帧渲染路径（paint/draw/walk_damage）与每次输入
  派发路径（dispatch/pick）——命中测试、属性设置、绘制。热路径新增
  代码**禁止引入分配**（vector 扩容、make_shared、临时 string/
  stringstream）。
- 闸门：`test/test_alloc_guard.cpp`（`test_alloc_guard` suite）锁定
  停放树重绘、slop 内 move dispatch、文本绘制、ListBox 滚动后的暖重绘
  为 0 分配；每次改动热路径代码后必须通过。
- 豁免与边界：
  - 日志宏在默认级别（debug）下构造 stringstream（时间戳 + 消息体）：
    热路径 LD 的分配为**已豁免的已知项**——`Logging::set_min_level`
    提到 info 及以上后，被抑制级别零构造零分配；闸门测试按
    默认级别钉住的是无日志的路径。
  - `focus_next` 的每按键临时 `std::vector`：实测 1011 节点
    ≈41µs，远低于 5% 帧预算证据门槛——按"条件任务"门槛处理，不预优化。
  - ListBox 行图像缓存（批次 J2）——重建本身<b>有界</b>（预算
    `row_cache_budget`，窗口 = 可见行），重建精确计数由
    `test_list_box` 锁定，稳态绘制 0 分配；动态 ItemText 内容变化的
    失效责任在调用方（调任意 setter）。
  - 初始化路径（构造、资源加载）不受本预算约束（§1）。
- 共享义务：BitmapProvider 进程级共享（批次 J6）建立在它无实例状态的
  前提上；给它加状态必须先移除共享。
- **USE_FONT 文本路径（A-8，2026-08-28）**：`Font::draw_alphamap`
  的逐字形 `resize` 属于热路径分配，当前仅 bitmap 路径被
  `test_alloc_guard` 覆盖。`USE_FONT=ON` 的文本绘制新增代码必须满足
  同等 0 分配要求（复用缓冲或批量 `reserve`），并在闸门中增加
  `USE_FONT` 变体（见 `ARCHITECTURE.md` A-8）。
- **ListBox 动态模型失效（A-9，2026-08-28）**：行缓存 key 为
  `(row,sel,w,h,fg,bg)`，不含字符串内容。`ItemText` 内容在不调任何
  setter 时变化，命中旧位图属调用方违规。调用方必须在内容变后调
  `set_item_text`（同参即可）或 `invalidate_row_cache` 语义的任意
  setter；未来若提供 `touch_row` 细粒度接口，本条随之收敛。
- **Shell 输入→重绘链路（A-5）**：壳层字符事件（`ch` 通道）
  必须经 `is_dirty → paint → present` 链路（含 Win `WM_CHAR` 的
  `send_input` 复用），否则 `TextInput` 的 `mark_dirty` 不会落盘
  到屏幕。

## 9. 重绘欠帧协议

帧生命周期、damage 传播（`walk_damage` 读时消费、绘制期 damage 存活
到下一帧）、事件驱动壳与轮询壳的一致性——架构定义见
ARCHITECTURE.md §4.1。本节只保留 API 级义务：

- 应用在输入路径之外改 UI（定时器/协议回调）不需要也不应该手动
  invalidate——damage 经树自动传导，`invalidate()` 仅为树外变化保留
  （玻璃/树外绘制等 widget 无法上报的场景）。
- `walk_clear_damage` 保留为全树重置原语；paint 不调用它。
- **光栅器 damage 硬裁剪约定（A-12/A-13）**：`Graphics::set_damage(l,t,r,b)`
  是半开区间（`r`/`b` 独占），`draw_area` 是闭区间；两者唯一的交汇点是
  私有谓词 `damage_contains(x, y)`（`draw_pixel` 直接用，`fill` 按同一边界
  钳位后相交、退化即早退）。`draw_image` 逐像素委托 `draw_pixel`，继承
  裁剪。因此 damage 模式下不经 `clip_safe` 的直接绘制也是安全的
  （`test_raster_damage` 固化）。
- 不变量：`subtree_dirty_` 为真的节点其所有祖先均为真（冒泡置链与后序
  重算共同维护），冒泡据此可提前终止（重复 setter 为 O(1)）。
