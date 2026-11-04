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

## 5. 进展账（每步更新）

- 2026-11-03：分工拍板（SIXEL 优先；组合层入树 `tianji/`；FI-07 维持
  关闭）；`docs/backlog.md` Batch FI 重构为「框架模块 → 组合层」两段；
  本文建立。**尚无代码**（先文档与拆解）。
- 2026-11-04：**FI-09 落地**——`ImageView` 控件（内容标识解码缓存 +
  letterbox）+ `Graphics::draw_image_scaled`（确定性最近邻）+ 内存源
  PNG/JPEG 解码 + HTML `<img>` 白名单与 html_embed 构建期打包。评审
  approve；desktop / codecs / 16bpp 三配置 battery 全绿。舞台画布的
  图片通道就绪，下一项 FI-10（SIXEL 确定性量化器）。
