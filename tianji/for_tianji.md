# 致天机的回信（for_tianji）

> 写信方：imprint 维护者 · 2026-11-03 建。这是 imprint 侧给天机的
> **决策 / 进展 / 待沟通**文档，天机同学直接读这里。需求原文仍以贵方
> `docs/for_imprint.md` 为准（FI-01…FI-10 逐条状态见其 §5 对应表）；
> 采纳项的实装细节与完整 UI 规格在 imprint 仓 `docs/backlog.md`
> Batch FI（该节自足，不要求读者先读天机仓）。

## 1. 分工与边界（2026-11-03 拍板）

- **imprint 提供 UI 与 I/F**：控件 + 壳 + 喂数据的接口；外部（天机内核）
  用我们来显示文字和图片。UI 与内核分离。
- **集成形态由天机自选**（原生 app / SIXEL 终端 / wasm 浏览器），对框架是
  同一棵控件树，我们三条都接。**我们当前的工作顺序 SIXEL 优先**——SIXEL
  壳至今没有认真做过 demo，天机客户端就是它的试金石。**NDS 不在本消费方
  范围。**
- **I/F 是消息级，不是 C-ABI**：FI-07（动态 C-ABI 面）维持关闭。天机内核
  经 feed 通道喂数据；UI 组合层放在 imprint 仓 `tianji/` 文件夹内。
- 客户端完整 UI 规格（左右分栏、逻辑画布、消息分类、确定性要求）以
  `docs/backlog.md` Batch FI 的 "The client, as specified" 小节为准。

## 2. 我们的工作顺序（框架模块先行，组合层收尾）

1. **FI-09 ImageView**：图片解码缓存 + 确定性最近邻缩放 + letterbox +
   运行时 `set_image`——舞台画布的硬前提；
2. **FI-10 SIXEL 确定性量化器**（复活项）：照片级场景图 + UI 色同帧不丢色，
   同帧同 DCS 字节；
3. **标注画布控件**：固定逻辑坐标空间 + 背景层 + 指针笔画层 + 笔画数据模型
   （供 feed 注入他人标注 / 供 I/F 中继）；
4. **FI-05 / FI-06 / FI-02 消息流三件套**：跟底 / 变高行虚拟化 / 选择+
   剪贴板接缝；
5. **FI-04 中文运行时字体配置文档 / A-25 非 BMP 配对渲染**；
6. **组合层**：feed I/F + mock 喂入器 + SIXEL 优先的客户端 app
   （左：消息流；右：800×600 逻辑画布舞台）。

## 3. I/F 提案 v0（请天机确认）

- **通道**：连接贵方现有 Unix socket 服务（`tianji/transport.py` 的
  RoomServer），JSON lines v1 协议，`subscribe`（含 `after_seq` 断线续传）
  消费 view / stage / event 流——**不发明新协议**，用贵方既有形状的子集。
- **自测**：我们做一个 mock 喂入器，回放贵方 `samples/run_子夜客栈.json`
  形状的转录，端到端自测不依赖贵方在场。
- **需确认**：①该子集消息形状是否够用（room_snapshot / view(seat|director)
  / stage / event）；②stage 图像资源走什么通道分发（随 worldpack？URL？）。

## 4. 需要天机确认 / 提供的事项

1. **场景图素材（最关键）**：worldpack 目前零图片资产，StageView 的
   background/props 恒为空。请定义：格式（PNG？）、基准尺寸（800×600？）、
   来源（AI 生成？手绘？）、分发方式；"还原现场"动画是否需要角色/道具
   精灵图（sprite）。
2. **stage.annotation 激活**：schema 已预留（贵方
   `docs/07-interface-contracts.md` 的 circle/overlays），权限 / audience /
   ttl 语义归贵方；我们提供笔画数据（逻辑画布坐标、颜色、宽度、所有者）。
3. **SIXEL 终端目标清单**：贵方用户面向哪些终端（xterm / kitty / wezterm /
   Windows Terminal…）？这决定我们的测试矩阵；终端字体 cell 尺寸影响整帧
   几何（每帧整幅 sixel 位图，非文字单元混排）。
4. **输入方式确认**：SIXEL 壳的键盘输入由终端直送 UTF-8（term_input 已
   解析），中文输入依赖终端自身的 IME 行为——请确认可接受，或给出目标终端。

## 5. 中文渲染配方（FI-04 回执）

天机 FI-04 问"中文怎么配"。两条路径，按文本来源选：

- **动态文本（对话流、AI 生成的台词——你们的主体）→ 运行时全字体**：
  1. 构建加 `-DUSE_TTF_RUNTIME=ON`（vendored stb_truetype，无外部依赖）；
  2. 进程内加载一个覆盖 CJK 的 TTF：`TtfFamily::from_file(path)`
     （宿主机）或 `from_memory(bytes, n)`（借用，须活得比 family 长）；
  3. 装到控件上：`set_font_family(family)`（进程级）+ 需要处
     `set_font_size(px)`，或逐子树 `set_glyph_provider(family.provider_for(px))`；
  4. **缓存定档（唯一要动脑的参数）**：`cache_budget` 默认 64KB 只够
     16px 下 ~240 个字（每条目 w×h 覆盖字节，汉字 16px ≈ 256B），
     对话流必然反复清空。传 **2MB（≈8k 条目，对话工作集）** 到
     **8MB（≈32k 条目，16px 下全 GB2312）**；按 `字数 × px²` 缩放。
     细节在 imprint 仓 `docs/code-contract.md` §2.4 "Cache sizing"。
- **静态 UI 文本（构建期就定死的标签）→ 构建期子集**
  （`TTF_FONT` + `TtfSubsetProvider`，零运行时开销）。对话流不要用
  这条——AI 生成的字构建期枚举不全。

兜底链：运行时字体未覆盖的字 → 5x7 位图（仅 ASCII）→ 跳过。
字体缺 emoji 时 emoji 会被跳过——非 BMP 码点的**成对渲染与光标/
选择边界**属 A-25/FI-03（本批稍后落地，届时补充）。

## 6. 进展账（每步更新）

- 2026-11-03：分工拍板（SIXEL 优先；组合层入树 `tianji/`；FI-07 维持
  关闭）；`docs/backlog.md` Batch FI 重构为「框架模块 → 组合层」两段；
  本文建立。**尚无代码**（先文档与拆解）。
- 2026-11-04：**FI-09 落地**——`ImageView` 控件（内容标识解码缓存 +
  letterbox）+ `Graphics::draw_image_scaled`（确定性最近邻）+ 内存源
  PNG/JPEG 解码 + HTML `<img>` 白名单与 html_embed 构建期打包。评审
  approve；desktop / codecs / 16bpp 三配置 battery 全绿。舞台画布的
  图片通道就绪，下一项 FI-10（SIXEL 确定性量化器）。
- 2026-11-04：**FI-10 落地**——SIXEL 确定性中位切分调色板：文字帧逐字节
  不变；照片级帧（任一带将超 250 寄存器）整帧切换帧级调色板，**零丢像素、
  无带间接缝、无告警**（同帧同 DCS 字节）。评审 approve；desktop +
  16bpp battery 全绿。SIXEL 优先路径的两块硬前提（图片缩放 + 保真出图）
  均已就绪。
- 2026-11-04：**标注画布落地（TJ-NEW）**——`AnnotateCanvas`（ImageView
  子类）：固定逻辑画布坐标空间（与背景 letterbox 共享同一映射）、笔画即
  数据（显式 id 注入远程席位标注 / 删除 / `stroke_ended` 中继）、启用时
  Slider 式指针捕获（默认只读舞台）、`Graphics::draw_polyline_aa` 宽线
  原语（SvgCanvas 胶囊算法提炼，定点确定性）。评审意见两条已修（注入点
  int64→int 窄化钳制 + 格式漂移）；desktop + 16bpp battery 全绿。舞台
  的"画线标注"框架能力就绪。
