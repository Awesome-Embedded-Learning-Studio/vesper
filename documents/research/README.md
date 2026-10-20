# 桌面环境调研索引

> 调研日 2026-10-17 · 服务于 vesper 桌面仓的刀序与架构拍板 · 两批共 27 个调研 Agent 产出（八主题并行起草、两篇限流缺稿补写、缺件分析、完整性审查、五篇裁决修补、九篇 content-forge single-editorial 轻润色）· 每篇文末附参考来源，关键断言带 URL

## 九篇与阅读顺序

| 序 | 文档 | 内容一句话 | 主要服务诉求 |
|---|---|---|---|
| 1 | [construction-order.md](construction-order.md) | KDE、GNOME、Xfce 起家顺序考证，八条带证据的先后律，对 D0-D6 的印证与两条修正 | 建设顺序 |
| 2 | [window-and-session-theory.md](window-and-session-theory.md) | 窗口八操作语义、焦点模型、工作区、合成器分工、Wayland 协议族、会话层，附 D1/D2 刀面边界建议 | 差的抽象（窗口本体） |
| 3 | [kde-plasma-architecture.md](kde-plasma-architecture.md) | 四层栈、plasmashell 与 KWin 进程分工、D-Bus 与配置存储、两次推倒重来的教训 | 架构参照 |
| 4 | [gnome-architecture.md](gnome-architecture.md) | 双 UI 栈、Shell 扩展机制、Core Apps 名册治理、HIG 四环链条 | 架构参照、App 名册 |
| 5 | [lightweight-and-classic-desktops.md](lightweight-and-classic-desktops.md) | Xfce 借件、LXQt 砍件、E 吞件三条省件路，Windows 95、classic Mac OS、CDE 概念件对照 | 概念件与 reference 素材 |
| 6 | [desktop-infrastructure.md](desktop-infrastructure.md) | D-Bus、XDG 规格、文本栈、剪贴板、输入法、portal 逐项三问：解决什么、没有它能不能活、vesper 怎么取舍 | 需要的 base |
| 7 | [terminal-emulator-internals.md](terminal-emulator-internals.md) | Konsole、VTE、alacritty 结构，bash 与 vim 实测序列子集，D3/D4 落地配方与验收判例 | 终端 App |
| 8 | [app-suite-survey.md](app-suite-survey.md) | 三家出厂 App 套件对比，最小可用 App 集，四件第一波 App 的依赖映射与顺序建议 | 需要做的 App |
| 9 | [vesper-gap-analysis.md](vesper-gap-analysis.md) | 35 条缺件清单（桌面本体 19、App 层 10、base 6），每条带现状出处、处置倾向、刀级 | 差的抽象、需要的工作 |

## 五项诉求地图

- 建设顺序：construction-order.md 第 6、7 节
- 差的抽象：vesper-gap-analysis.md 全篇，window-and-session-theory.md 作窗口本体的理论底稿
- 需要的工作：vesper-gap-analysis.md 第 4 节刀级估计与回填分簇时机
- 需要做的 App：app-suite-survey.md 全篇
- 需要的 base：desktop-infrastructure.md 全篇

## 本次调研已裁事项（作者可翻案）

- 剪贴板口径：PRIMARY 语义（拖选写入加中键粘贴）随 D4 终端接线做，CLIPBOARD 与拖放归任务栏或 WM 刀次。三篇原文各有口径，批二统一，见 terminal 篇第 4、6 节与缺件清单 1.2。
- VTE rewrap 版本：完整性审查曾裁为 0.53（2018），修补 Agent 以仓库 git 历史（提交 01380dae，2013-11-18）加 tag 0.35.1 的 NEWS 等四方一手证据推翻，正文保留 0.35（2013），证据链见 terminal 篇参考来源。

## 悬而未决（等作者拍板）

> 2026-10-20 注：第二束里的双击手势（1.18）与容器子表容量（1.19）已随 visor `49f95c0`（双击手势与身份会话）和 `5bc71dc`（孩子表改侵入式兄弟链表）在上游闭环，本仓不再持有；其余各项仍待拍板。

- D 系列加两刀的建议：只读窗口列表条（面板雏形）插 D2 与 D4 之间，极简 ini 配置读写插 D0 至 D1。construction-order.md 修正一、二与缺件清单 1.8、1.9 独立得出同一结论。
- D2 前要提给 visor 的上游决策一束：双击手势（1.18）、容器子表容量（1.19）、弹层挂载（1.1）、事件 ABI 簇（1.4 修饰键加 1.7 滚轮，建议合一刀提）。
- kde 篇 4.1 节写「装饰实际有三条路」但正文只列两条，润色走查按铁律未动事实层，待作者核对补齐。
