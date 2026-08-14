# third_party 目录纪律

vendored 第三方单头文件库。裁决背景见 `CONTEXT.md` 批次 D：vendor 是
行业常态（Dear ImGui 内置 stb_truetype / Qt 内置 zlib），默认构建仍零
依赖（`USE_*` 保持可选）。

## 现状

| 文件 | 版本 | 用途 | 实现入口 |
|---|---|---|---|
| `stb/stb_image.h` | v2.30 | PNG/JPEG 解码（`USE_PNG`/`USE_JPEG`） | `imcore/src/codec/stb_impl.cpp` |
| `stb/stb_image_write.h` | v1.16 | PNG/JPEG 编码（写盘/截图） | 同上 |
| `stb/stb_truetype.h` | v1.26 | 构建期 TTF→位图（批次 E 字体子集工具链，仅头文件，未接实现） | — |

## 规则

- 文件直接从上游下载，**禁止改动**（含格式重整）；补丁一律放
  `imcore/src/codec/` 一层 wrapper，vendor 目录保持纯净。
- 文件头版权声明（public domain / MIT with no warranty）保留。
- 升级 = 整文件替换 + 更新本表版本号。
- 实现入口只允许出现在框架库内的 wrapper TU（`STB_*_IMPLEMENTATION`
  不跨 TU 重复定义）。
- 默认构建不含 vendored 代码：只有 `USE_PNG`/`USE_JPEG` 打开时才编译进
  imcore，零依赖基线不破坏。