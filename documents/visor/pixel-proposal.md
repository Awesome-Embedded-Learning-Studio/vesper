# visor 像素层提案底稿：三档像素与真彩渲染核（束 D 主体）

> 起草日 2026-10-18 · 状态：已销案——上游以 `d8fc89f feat: 像素层三档，画布按格式实例化` 落地（2026-10-20 核对：pixel_traits、Canvas 按格式实例化、彩色预设、枚举三档、转换矩阵、X11 吃 RGB565、pixel_tests 全数到位） · 依据：documents/visor/upgrade-design.md 第 1.1、2、4 节，2026-10-18 彩色方向拍板，对 visor HEAD `4174f87` 逐文件核对

## 0. 这份文档是什么

束 D（像素层簇）的完整提案底稿。2026-10-18 作者拍板桌面走彩色、RGB565 站立项，upgrade-design 第 4 节排的「RGB565 站立项时提束 D 主体」就此到期。本文写到可以直接带进 visor 会话当上游提案的细度。

## 1. 目标与非目标

目标：渲染核直接产三档像素（Mono1、RGB565、XRGB8888），编译期选档，运行时零转换零分派。控件只说 ColorRole 的习惯不变，主题表从黑白预设长出彩色预设。

非目标：8 位调色板档（拍板的三档之外不开，小 RAM 场景由 Mono1 档加分块探针覆盖）。运行时切档。图像文件加载（vesper 侧另案，等本提案的格式落定再动）。

## 2. 现状核实（HEAD `4174f87`，逐文件看过）

- 语义层已是彩色形状：`canvas/color.hpp` 的 Ink 是 32 位 ARGB（`from_rgb`、`black`、`white`），`ui/theme.hpp` 的 Theme 七角色各存一个 Ink，kLightTheme 是黑白预设。
- 量化点已收口并自我预告：`ui/canvas_painter.hpp` 把七角色一次解析成表，写像素时过 `canvas::mono_ink`，注释原话认领这是 Mono1 的 trait in embryo、format chapter 会把它搬走。本提案就是那个 format chapter。
- 画布仍单色：`canvas/mono_canvas.hpp` 的 `Canvas<Size>` 一比特按位打包，每行 (w+7)/8 字节。
- 传输缝格式无关：`bindings/display.hpp` 的 PixelFormat 枚举现单成员 Mono1，FlushRegion 带 format 字段，宿主表零改动。
- 墨模资产格式无关：MonoFont 与 MonoBitmap 是 1bpp 位图，按当前墨色着色喷射，三档通用。
- 宿主：X11 宿主收 1bpp 自行扩成窗口像素。cinux 绑定的 FrameTarget 已有 1bpp 扩 XRGB8888 当先例。

抽象层与量化点都已就位，要动的是画布打包、画笔的 traits 化、枚举扩员、宿主写色，传输接口不动。

## 3. 设计形状

### 3.1 pixel_traits 模板

每档一份特性表：像素的读写语义（Mono1 读回 0 或 1，RGB565 是 16 位 r5g6b5，XRGB8888 是 32 位）、每行字节数、行打包细节（Mono1 保持现行按位打包，不动内存形状）、Ink 到本档像素的映射（Mono1 沿用非黑即亮，RGB565 取 R 高五位 G 高六位 B 高五位，XRGB8888 直取）。traits 无堆、constexpr 全覆盖，与 visor 家风同构。

### 3.2 Canvas 与 Painter 按 traits 实例化

`Canvas<Size, Format>`，Pen 与 Painter 吃 traits，Mono1 成为第一个实例化而非唯一形状。canvas_painter 的七角色表存 Ink 不变，写像素那一步从 `mono_ink` 换成 traits 的映射，注释里说的搬走落在这一步。

### 3.3 主题与彩色预设

Theme 结构不动（已是 Ink 七色）。新增彩色预设：七角色给真彩值，具体色值作者过目时拍，示例给 Accent 一个鸢尾蓝（`Ink::from_rgb(0x4a62c8)` 一类），Foreground 近白、Background 近黑，保住单色档沿用的极性习惯。控件代码零改动。

### 3.4 枚举与转换

PixelFormat 加 RGB565 与 XRGB8888（束 D 的半步，upgrade-design 第 4.1 节建议的占名分）。转换矩阵：已有 1bpp 扩 XRGB8888 当先例，补 RGB565 双向、XRGB8888 量化到 RGB565（Wayland 服务端站收 32 位应用缓冲时用）。

### 3.5 宿主

X11 肉眼通道先行：开真彩窗口，RGB565 档直接写，桌面立刻看得见彩色。cinux 绑定按需换向（核内 RGB565 时，扩色方向变成 565 到面板）。vesper 的 fake host 同步扩 RGB565 格式与读色（见第 6 节）。

## 4. 内存算术（480x320 站）

| 档位 | 整帧 | 64 行条带 |
|---|---|---|
| Mono1 | 18.75 KiB | 3.75 KiB |
| RGB565 | 300 KiB | 60 KiB |
| XRGB8888 | 600 KiB | 120 KiB |

小 RAM 彩屏分块探针（PIN.md vendor 三前置之一）即 RGB565 档在 i.MX6ULL 上的条带实弹。

## 5. 判例

- Mono1 档全部现有判例不动，回归即验收。
- RGB565 档新增：基元判例（填充、圆角、线条按色值读写像素）、字形与图标的着色喷射判例（墨模乘墨色）、主题切换判例（七角色表翻转、重画上报）。
- 转换判例：三档两两转换的往返断言。
- 交叉编译判例：i.MX6ULL 目标入判例集（upgrade-design 第 1.3 节的判例机）。
- 编译矩阵收口：判例默认编 Mono1 加 RGB565 两档，XRGB8888 由 PC 后端与 Wayland 服务端站编，避免全矩阵编译时间爆炸。

## 6. 对 vesper 的影响

- fake host 扩 RGB565：PixelFormat 一员，blit 与 get 按两字节读写，判例的像素断言从布尔改读色值。
- 桌面控件与判例逻辑零改动，代码只说角色。
- 落地流程照旧：visor 合并后重跑两边验收，更新 PIN.md 的 tested-against，与本次 4174f87 同流程。

## 7. 排期建议

彩核插在 D4 前。终端的 ANSI 颜色子集（D4 按 bash 实际输出补）在 RGB565 档才有真色值，核先落地终端后吃，顺序别倒。X11 肉眼通道先行，作者随时看得见效果。

## 8. 开放问题（作者过目时拍）

- 彩色预设的具体色值（本文只给了 Accent 的鸢尾蓝示例，七角色全表待拍）。
- 判例默认编档组合 Mono1 加 RGB565 是否同意（upgrade-design 第 5 节同问）。
- i.MX6ULL 交叉编译判例与彩核同期动还是随后补。
- X11 宿主先吃 RGB565，还是等 cinux 站一起动。

## 9. 来源

- documents/visor/upgrade-design.md（第 1.1 节编译期策略、第 2 节像素层细则、束 D 清单、第 4 节提案节奏）
- visor HEAD `4174f87` 逐文件核对：canvas/color.hpp、ui/theme.hpp、ui/canvas_painter.hpp、canvas/mono_canvas.hpp、bindings/display.hpp
- 2026-10-18 对话拍板：桌面彩色定案、图像加载分道（墨模先行、彩色解码等格式落定）
