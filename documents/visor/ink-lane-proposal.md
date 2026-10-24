# visor stage-4 提案底稿：像素动词的裸墨通道与逐笔彩色文本

> 起草日 2026-10-24 · 状态：已并入上游（visor main 至 73d76d1，2026-10-24。落地偏离：虚层翻转改为并排 Ink 重载，因为角色表是构造期一次性预折的目标像素，翻转会把热路径退化成逐调用折算。run 合批仍不做） · 依据：vesper 终端线（D3 至 D5b）与 C 系列的实现经验，本仓分支 compositor-c-series。前例：第四口 spawn（documents/visor/spawn-fourth-slot-proposal.md，已并入上游）。

## 0. 这份文档是什么

给 visor 会话的上游提案底稿，对应 vesper 待补清单里的一句「终端 16 色 SGR 仍候 visor 逐笔墨色原语」。终端要支持 SGR 转义（程序用它指挥终端变色），最后一环卡在 visor 的画字与画笔动词只认主题角色色，没有裸颜色进像素的通道。这份底稿把消费侧的形状写清，照 spawn 那单的经验，上游会话接得快。

## 1. 消费侧现状与卡点

终端今天怎么画（src/ui/terminal/terminal.cpp 的 on_draw）：整盒先铺一个角色色当底，然后逐格画字，一格一次 draw_text_clipped，全串一个 ColorRole，最后光标格再铺一个角色色。三样全走主题槽，能表达的颜色由 desk 的配色方案说了算。

SGR 要什么：程序发 `ESC[31m` 意思是接下来的字用红画，`ESC[41m` 是接下来格底铺红，`ESC[0m` 复原。十六色表是 xterm 定死的 rgb 值（红是 0xcd0000 一类），不是任何主题槽。也就是说终端要能对每个格说「这个字用这个 rgb」「这格底用这个 rgb」，而现在 Painter 的全部动词（fill、plot、draw_bitmap、fill_rounded）只吃 ColorRole，canvas::Ink 这个 32 位颜色类型在像素动词层没有入口。

## 2. 提案

两步，一步底座一步消费面。

第一步，底座翻转。fill、plot、draw_bitmap、fill_rounded 四个动词的虚层换成 Ink 版本，ColorRole 版退成薄包装：查一次 scheme 把角色折成 Ink，再转发给 Ink 版。虚函数总数不变（实现侧从「实现角色版」变成「实现 Ink 版」），角色与像素的换算从各 Profile 挪进一处，将来任何数据驱动的颜色（终端调色板、图像旁路、取色器）都自动有了通道。

第二步，文本动词加 Ink 重载。draw_text 与 draw_text_clipped 各加一个 Ink 参数版（参数类型不同直接重载，名字不动）。文本是自由函数不是画笔动词，零接口负担。不需要 run 结构：终端本就逐格画，一格一调在列，按色段合批是将来测量说要再谈的优化，第一刀不做。

## 3. 为什么归 visor

角色表与像素换算是画布与画笔边界内的事，主题词汇是 visor 的。若 vesper 自己私养一条裸色旁路，等于绕过主题层直接戳画布，三级宿主梯队（fake、X11/fbdev、Cinux）各答各色的纪律就破了。翻转后 mono 宿主照旧把彩色压成亮暗（判例走 fake），Cinux 帧扩张照旧查自己的表。

## 4. 边界与不做的事

不做 run 与批量文本 API，消费者还没要。不做文本动词里的背景合绘制，终端自己 fill 格底。不做加粗联动加亮一类的 SGR 语义联动，那是 vesper 解析层的事，visor 只递颜色。不动其余动词的语义与签名（除薄包装化），mono 判例的行为零变化。

## 5. 判据

- visor 侧：offscreen 色画布上，Ink 版 fill 与 draw_text 出字面像素值（判例直接读像素比对），全套既有 role 判例保持全绿，证明薄包装无损。
- vesper 侧（回填后）：终端吃下 `ESC[31m` 后对应格的字画出 xterm 红的 rgb 值，`ESC[41m` 的格底铺红，`ESC[0m` 复原成主题色，判例全绿。十六色表常量住 vesper，visor 不内置终端调色板。
- mono 侧：彩色请求折成亮暗不炸，现有 mono 判例不回归。

## 6. 时机

visor 侧无前置依赖，任意时点可做。vesper 侧回填在上游合并、指针前移之后。
