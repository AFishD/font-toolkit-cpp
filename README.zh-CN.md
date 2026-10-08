# ftk — C++ 字体工具库

一个零外部依赖的 **C++17** 字体工具包（库 + 命令行工具 + **插件**），用高性能、
线程安全的方式重写了 Python [fontTools](https://github.com/fonttools/fonttools) 的核心功能。

它以「可嵌入」为设计目标：提供稳定的 **C ABI** 与 C++ API，不依赖第三方库；
字体对象在打开后不可变，因此同一个 `Font` 可安全地被多线程共享。

* **容器格式** — TTF、OTF、TTC、WOFF、WOFF2（内置 Brotli 解码器）
* **轮廓** — TrueType `glyf`（复合字形、点匹配）、CFF、CFF2
  （Type 2 charstring、subrs、CID、`seac`、blend）
* **可变字体** — fvar、avar、STAT、gvar（共享/私有点表、打包增量、IUP、
  phantom 点）、HVAR、VVAR、ItemVariationStore
* **常用表** — head、hhea、maxp、hmtx、OS/2、post、name、cmap（格式
  0/2/4/6/12/13/14）、kern、gasp、VORG
* **布局与彩色字体** — GSUB/GPOS/GDEF 检视、字距、COLR/CPAL、sbix、
  CBDT/CBLC、SVG
* **功能** — TTX 风格 XML 导出、JSON、子集化（TrueType + CFF/CID）、
  WOFF 转换、并行批量扫描、校验和验证
* **并发** — 内置线程池；在 3.9 GB / 993 万字形的语料上，1 → 12 线程约 4 倍加速

## 构建

需要 CMake ≥ 3.16 与支持 C++17 的编译器（已在 MinGW-w64 g++ 8.1 + CMake 4.0 /
Ninja 上验证；Linux 下的 GCC/Clang 与 MSVC 同样适用）。

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

产物：`build/libftk.dll`（Linux 下为 `.so`，即库/插件）与 `build/ftk`
（命令行工具）。追加 `-DFTK_BUILD_DEMO=ON` 可一并编译
[`docs/demo.c`](docs/demo.c)。

## 快速开始

```bash
ftk info MyFont.ttf                                # 字体信息、度量、表列表
ftk outline MyFont.ttf --char U+4E2D --svg         # 取字形轮廓（可输出 SVG）
ftk var Variable.ttf                               # 可变字体轴与命名实例
ftk dump MyFont.ttf -o font.ttx                    # 导出 TTX 风格 XML
ftk subset MyFont.ttf -o mini.ttf --unicodes 0x41-0x5A,0x4E00-0x4E20
ftk convert MyFont.ttf -o myfont.woff2             # 容器格式转换
ftk scan Fonts -j 12                               # 多线程批量扫描
```

在 C 中调用本库：

```c
#include "ftk/ftk.h"

ftk_error err;
ftk_font* f = ftk_open_file("MyFont.ttf", 0, &err);

ftk_font_info info;
ftk_get_font_info(f, &info);

uint16_t gid = ftk_glyph_for_unicode(f, 0x4E2D);
size_t n_segs;
float* segs = ftk_glyph_outline(f, gid, NULL, 0, &n_segs);  /* 每段 8 个 float */

const double coords[1] = {700.0};                           /* 每个 fvar 轴一个值 */
double advance = ftk_glyph_advance(f, gid, coords, 1);

ftk_free(segs);
ftk_font_close(f);
```

完整可编译示例见 [`docs/demo.c`](docs/demo.c)，全部 API 说明（C ABI、C++ API
与命令行）见 [`docs/API.md`](docs/API.md)（英文）。

## 文档

| 文件 | 内容 |
|---|---|
| [`docs/API.md`](docs/API.md) | 全部 API 功能：C ABI、C++ API、命令行 |
| [`docs/demo.c`](docs/demo.c) | 插件接入示例（打开 → 字形 → 子集化 → 并行扫描） |
| [`README.md`](README.md) | English version |

## 许可证

MIT，见 [LICENSE](LICENSE)。第三方代码与 fontTools 参考实现的署名见
[THIRD_PARTY.md](THIRD_PARTY.md)。
