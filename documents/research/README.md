# 桌面环境调研导读

> 调研日 2026-10-17 · 八篇 · 每篇文末附参考来源，关键断言带 URL，查不到出处的明写

这组调研回答一个问题：一个能叫「桌面」的东西由哪些件组成，成熟桌面是怎么把它们长出来的。材料以一手为主（原始公告、规范原文、论文、手册页），二手转述均在各篇参考来源里注明。

## 八篇与阅读顺序

| 序 | 文档 | 内容一句话 |
|---|---|---|
| 1 | [construction-order.md](construction-order.md) | KDE、GNOME、Xfce 起家顺序考证，旁证 weston、i3、sway，八条带证据的先后律与对从零起步项目的提示 |
| 2 | [window-and-session-theory.md](window-and-session-theory.md) | ICCCM 与 EWMH 定下的窗口语义、焦点模型、工作区、合成器分工、Wayland 协议族五件、登录会话链 |
| 3 | [kde-plasma-architecture.md](kde-plasma-architecture.md) | Qt 到 Gear 的四层栈、plasmashell 与 KWin 进程分工、D-Bus 与配置存储、两次推倒重来的教训 |
| 4 | [gnome-architecture.md](gnome-architecture.md) | 双 UI 栈、Shell 扩展机制、Core Apps 名册治理、HIG 四环链条、1.x 到 50 的演化史 |
| 5 | [lightweight-and-classic-desktops.md](lightweight-and-classic-desktops.md) | Xfce 借件、LXQt 砍件、E 吞件三条省件路，Windows 95、classic Mac OS、CDE 概念件对照 |
| 6 | [desktop-infrastructure.md](desktop-infrastructure.md) | D-Bus、systemd 用户实例、PipeWire、XDG 规格家族、文本栈、剪贴板、输入法、portal 逐项三问：解决什么、没有它能不能活、小桌面怎么取舍 |
| 7 | [terminal-emulator-internals.md](terminal-emulator-internals.md) | Konsole、VTE、alacritty、kitty 的构造，PTY 链路全走一遍，bash 与 vim 实测序列子集，自研最小终端的做与不做清单 |
| 8 | [app-suite-survey.md](app-suite-survey.md) | 三家出厂 App 套件对比，最小可用 App 集，候选 App 的依赖面分析与建设顺序 |

顺序建议：想懂「桌面是什么」先读 5（概念件对照）再读 2（窗口语义底册）；想懂「成熟桌面怎么长的」读 3、4；想自己做一个，读 1（先后律）、6（地基取舍）、7 与 8（第一个 App 怎么挑）。

## 全组共同的方法论

- 借概念，不搬实现。所有概念借用只借语义（比如 EWMH 的窗口类型学），线格式与代码形状各走各的路。
- 断言带出处。日期、版本号、引语尽量给一手来源 URL，推断处标明是倒推，查不到出处的明写「未附单一出处」。
- 反例与修正并录。每条结论附跨项目证据，先例里没有对应的做法照实写「比历史先例激进」。

## 勘误待核

- kde 篇 4.1 节写「装饰实际有三条路」但正文只列两条，待核对补齐。
