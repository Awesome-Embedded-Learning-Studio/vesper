# 桌面环境建设顺序考证：KDE、GNOME、Xfce 与窗口管理本体项目的起家史

> 调研日期 2026-10-17 · 背景：手搓教学桌面项目的立项调研 · 来源：网络检索加本地材料

## 1. 研究问题与材料口径

本文回答一组具体问题：KDE、GNOME、Xfce 各自最早长出来的是库还是 App，窗口管理器什么时候进场，从小项目长成完整桌面的路径长什么样。旁证取 weston、i3、sway 三个窗口管理本体项目，看它们与「桌面环境」类项目的顺序差别。最后提炼从零起家的通用先后律，并给今天从零起步的项目三条提示。

材料以一手为主：Matthias Ettrich 1996 年的 KDE 原始公告、Miguel de Icaza 1997 年的 GNOME 原始公告、Olivier Fourdan 2000 年的 USENIX 论文、《20 Years of KDE》书、i3 官方下载页与 4.0 发布说明、Drew DeVault 的 sway 1.0 与 wlroots 文章、weston 手册页。二手转述仅在无法取得一手时使用，均在参考来源里注明。日期与版本号一律按来源原样记录，推断处标明是我倒推的。

## 2. KDE：面板起家，窗口管理器计划殿后、实际提前进场

1996 年 10 月 14 日 Ettrich 在 de.comp.os.linux.misc 发帖《New Project: Kool Desktop Environment (KDE)》。公告发出时已经动手做的是面板，他原话是「I started to work on this panel」。技术路线写得清楚，面板起初当 FvwmModule 跑（「Run's as FvwmModule (at the beginning)」），也就是寄居在现成的 fvwm 里，而不是先自建基础设施。来源：kde.org 存档的公告原文。

公告附了一张项目清单，顺序本身就是一份建设优先级：

| 优先级 | 项目 | 公告里的定位 |
|---|---|---|
| 1 | Panel | 「The basic application」，作者本人已开工 |
| 2 | Filemanager | 「Another major application」，明确说不是 bash 图形替代品 |
| 3 | mail client、text editor、Terminal、image viewer | 第一批 App。Terminal 的做法是「get the xterm sources and add a GUI with Qt!」，给 xterm 源码套 Qt 壳 |
| 4 | Hypertext Help System、System Tools、Games | 外围 |
| 末位 | Window Manager | 原话「But this may come very last」，思路是先做面板，等面板成了再从 fvwm 里剥出一个小 WM |

实际走向和计划不同。David Faure 在《20 Years of KDE》里回忆，「the very beginning was a window manager (kwm), a panel (kpanel), and the first library classes where KConfig … and KApplication」。Richard Moore 也说 1997 年前后他加入时「it consisted of a window manager (kwm), a file manager (kfm) and a panel to launch things (kpanel)」。也就是说窗口管理器没有等到最后，第一年就和面板、文件管理器、最早的一批库类（KConfig 的格式仿 win.ini）一起出现了。KDE 第一个协调发布是 Beta 1（1997-10-20），距公告整一年，KDE 1.0 于 1998-07-12 发布。

为什么计划落空。Martin Gräßlin 在同书里点破：Ettrich 当年并不想写专用 WM，想的就是「面板当 FvwmModule」，但面板要任务列表、要虚拟桌面切换、要统一的观感，这些语义全都压在 WM 身上，借用别人的 WM 顶不住，于是 kwm 在第一年进场，接着 KDE 2 时代（1999 年动工）换成彻底自写的 KWin，后者成了 Plasma 里最古老的组件之一。这段「WM 计划殿后、实际提前」的反复，是对后来者最有价值的证据。

## 3. GNOME：工具箱是继承来的，自己的窗口管理器等了约五年

GNOME 的起点与 KDE 互补。1997 年 8 月 15 日 Miguel de Icaza 在 GTK 邮件列表宣布立项（8 月 28 日的转发件存于 Debian 列表存档），动机是 Qt 许可证问题，路线是「We want to develop a free and complete set of user friendly applications and desktop tools」，工具箱直接用现成的 GTK——公告里注明 GTK 是 Peter Mattis、Spencer Kimball、Josh MacDonald 为 GIMP（1996 年）写的库。也就是说 GNOME 的控件库先于 GNOME 项目整整一年就存在了，而且根本不是为桌面写的。

公告有一个醒目的缺席：通篇不提窗口管理器。GNOME 从第一天就不打算自己解决 WM。后续时间线：

| 时间 | 事件 | 该件的出身 |
|---|---|---|
| 1996 | GTK 随 GIMP 存在 | 继承（GIMP 项目） |
| 1997-08 | GNOME 立项，宣布复用 KDE 代码、给现有 GNU 工具套一致前端 | 计划本身就是借用 |
| 1999-03-03 | GNOME 1.0： panel 加 applets、Control Center 加 capplets、文件管理器 gmc | gmc 是 Midnight Commander 的 GNOME 版，借用。WM 用 Enlightenment，Wikipedia 词条原话是当时「no official window manager」，Enlightenment 只是默认且唯一完全兼容的 |
| 2000-05-25 | GNOME 1.2 「Bongo」 | Sawfish 时期开始（外部作者作品被收编） |
| 2001-04-02 | GNOME 1.4 「Tranquility」 | Nautilus（Eazel 公司新写）接替 gmc，词条说 GNOME 1.x 到最后阶段才有官方 WM（Sawfish） |
| 2001 起 | Havoc Pennington 写 Metacity，GNOME 2.2（2003-02）接棒 | 第一件亲生的 WM，其项目页自述为「the default window manager for the GNOME 2.x desktop」 |
| 2011 | GNOME 3 | Metacity 演化为 Mutter，与 GNOME Shell 合体 |

另一个顺序证据藏在名字里。GNOME 全称 GNU Network Object Model Environment，立项时想先做一套 COM 式的组件模型，Wikipedia 词条说这个构想「was eventually released, but became known as 'Bonobo'」，即推迟到多年后才以 Bonobo 之名实现。桌面壳先立住，组件模型后补课。

GNOME 的顺序总结：先有别人的工具箱，再有面板与设置件，文件管理器先借 Midnight Commander 后由专职公司（Eazel）重写，自己的 WM 从立项算起约五年（2001 年的 Metacity）才动手。企业投入（Red Hat 雇 Pennington、Eazel 做 Nautilus）明显加速了中期件。

## 4. Xfce：一根任务栏长成一座桌面

Xfce 是「从极小种子长成完整桌面」的最佳标本，Fourdan 本人 2000 年的 USENIX 论文《Xfce: A lightweight desktop environment》给了一手叙述。

| 时间 | 事件 | 来源 |
|---|---|---|
| 1996 末 | Fourdan 立项，目标是 Linux 上的 CDE 式桌面 | Wikipedia「Xfce」 |
| 1997 初 | 开始编码。论文原话「My first goal was to write a toolbar similar to the CDE panel」，当时「there was nothing more than the xfce toolbar. It was interfacing with fvwm, using fvwm's module standards」，工具箱用 XForms | USENIX 论文 |
| 1997 | 第一版发布，Wikipedia 描述为「just a simple taskbar」，放上 SunSITE | Wikipedia「Xfce」 |
| 1998 | Xfce 2 带来第一版自有 WM「xfwm」。论文说它是改 fvwm 改出来的（「I made a lot of changes in fvwm to make xfwm」），且「the window manager took almost as much time coding as the toolbar itself」 | USENIX 论文、Wikipedia |
| 1999-03 | 因为 XForms 不开源，开始基于 GTK 的完全重写（3.x 线） | Wikipedia「Xfce」 |
| 2003-09-25 | Xfce 4.0，升到 GTK 2 | Wikipedia「Xfce」 |
| 2007-01 | 4.4： Thunar 接替 Xffm 成为文件管理器，桌面图标这才进来 | Wikipedia「Xfce」 |
| 2009-02 | 4.6： 新的配置后端与 settings manager | Wikipedia「Xfce」 |

Xfce 路径的要点。种子是一根任务栏，不是 WM、不是库、也不是 App。前两年寄居在 fvwm 的模块协议上，与 KDE 公告里的 FvwmModule 思路完全同款。自有 WM 是改 fvwm 而来，代价与工具栏本身相当。真正的完整文件管理器（Thunar）距立项十年，统一设置中心距立项十二年，都比直觉晚得多。2000 年时 Xfce 的全家福是：工具栏、xfwm、xftree（文件管理器，借树形）、xfclock、xfpager、xfbd（背景）、xfsound、xfmouse、一个 setup 对话框——一个工具集合，不是一套框架。

## 5. 旁证：weston、i3、sway 的另一种顺序

桌面环境类项目先有壳件后补核心，窗口管理本体项目恰好相反，核心最先、桌面性最后。

weston 是 Wayland 项目的参考合成器（NVIDIA 文档称其为 Wayland 项目的组成部分、参考实现）。手册页对其结构的描述值得整个借走：合成器本体是「a display server, a window manager, and a compositor all in one」，显示后端（drm、wayland、x11、rdp、headless）全部是可装载模块，而「桌面性」被压进 shell 插件层——「Weston's user interface is implemented by individual 'shell' plugins」，默认 desktop shell 提供「a panel with launchers and a clock, a background, and an interactive task switcher」，且手册特意声明它「not intended to be a full-fledged desktop environment in and of itself」。也就是说 weston 先把协议核心与后端做扎实，面板与启动器只是示范插件。来源：Arch Linux 的 weston(1) 手册页、Collabora 的 Weston 12 发布文（libweston 与可执行文件的分层）。

i3 是纯 WM 项目。首个版本 3.α 于 2009-03-15 发布（i3 官方下载页年表），整个项目从零用 C 写成、基于 libxcb，动机是 wmii 缺它想要的多显示器等特性（Debian 手册页语）。它头两年只有一条内置的工作区条，到 4.0（2011-07-31）才把状态条独立成 i3bar，发布说明原话「The internal workspace bar is obsolete. Use i3bar instead.」。核心稳定两年后，生态件才开始分家。

sway 是 i3 兼容的 Wayland 合成器。Wikipedia 记录首发 2016-03-24，作者 DeVault 在 1.0 发布文（2019-03-11）里说「1,315 days after I started the sway project」，倒推项目起点约 2015 年 8 月。它先建在单块的 wlc 库上，2017 年 12 月 DeVault 转而自建模块化的 wlroots（Phoronix 当日报导），2018 年 5 月发文《Why did we replace wlc?》解释 wlc 单块难改、wlroots 是「a set of modular tools with which you build the Wayland compositor yourself」，随后整个重写，1.0 于 2019-03-11 发布，同期才聚起 swayidle、swaylock、waybar、mako 等伴生生态。sway 的证据有两层：基建库值得为质量推倒重来一次，且重写发生在 1.0 之前。来源：sway 1.0 发布文、DeVault 博客、Phoronix、Wikipedia「Sway (window manager)」。

## 6. 从零起家的通用先后律

以下每条律都附跨项目证据，反例与修正一并写出。

律一，库与工具箱永远先于桌面件，且常常先于桌面项目本身存在。GTK（1996，GIMP）先于 GNOME（1997）一年。KDE 公告把选 Qt 当作第一技术决策（「Since a few weeks a really great new widget library is available」）。Xfce 1999 年宁可完全重写也要从 XForms 换到 GTK。sway 为质量自建 wlroots 再谈 1.0。没有一个项目是先写 App 再选工具箱的。

律二，桌面环境类的第一件桌面件是面板或文件管理器，几乎从来不是 WM。KDE 公告里 Panel 是「The basic Application」且已开工。Xfce 第一版只有一根任务栏。GNOME 1.0 的门面是 panel 加 Control Center。反例方向：LXDE 2006 年以文件管理器 PCManFM 为第一个模块起家（Linux Wiki 与 Wikipedia 词条），说明种子也可以是文件管理器。窗口管理本体项目（i3、sway、weston）则相反，窗口管理与合成是项目本体。两条路合成一句：第一件是「用户每天要看的东西」，面板或文件管理器二选一，WM 只有在项目本体就是 WM 时才先行。

律三，自己的 WM 普遍走「借、改、写」三段，晚到是常态。KDE：计划用 fvwm 模块寄居（借），一年内 kwm 进场（据《20 Years of KDE》对 kwm 的记载，及 Gräßlin 所述 Ettrich 本不想写 WM），KDE 2 换自写 KWin（写，1999 动工）。GNOME：Enlightenment（借）到 Sawfish（外部收编）到 Metacity（亲写，2001）。Xfce：接 fvwm 模块（借）到 xfwm 改自 fvwm（改，1998）到后来自成一体。三大环境没有一个第一版 WM 是从零亲写的，全部从 fvwm 或他人成果起步。

律四，文件管理器是中期件与重写大户。KDE 的 kfm 第一年在，KDE 2 演化为 Konqueror 并生出 KIO（Faure 原话「kfm, which became Konqueror and also gave birth to KIO」）。GNOME 的 gmc（借）到 Nautilus（2001，Eazel 重写）。Xfce 的 xftree 到 Xffm 再到 Thunar（2007）。LXDE 的 PCManFM 是唯一当种子的，但那是项目围绕它长，不是它先做完。

律五，终端在第一批 App 里，第一版多为套壳。KDE 公告明写 Terminal 的做法是取 xterm 源码加 Qt 图形界面。Konsole 的版权起点是 1997（Lars Doelle，Konsole 手册页版权行），即 KDE 起家第一年内终端已开工。GNOME 1.0 随附 GNOME Terminal。终端是桌面自证「能用」的最短路径，各家的第一版都不追求完整仿真，先跑起 shell 再说。

律六，配置的读写库很早，设置 GUI 中心很晚。KConfig 是 KDE 第一批库类（Faure 回忆），格式仿 win.ini，早于几乎所有 App。Xfce 2000 年只有一个 setup 对话框，现代 settings manager 到 4.6（2009）才来，距立项十二年。反例是 GNOME 1.0 就有 Control Center 加 capplets，企业投入（Red Hat）把这件常规后到件提前了。weston 至今只是一个 weston.ini。

律七，基建的推倒重来都发生在大版本边界，1.0 之前最常见。Xfce 的 XForms 到 GTK 重写（1999）与 GTK1 到 GTK2（2003）。sway 的 wlc 到 wlroots 重写（2017 至 2018，1.0 前）。KDE 2（2000）一次性换 kfm 为 Konqueror、kwm 为 KWin、立 KParts 与 KIO。GNOME 2 立新 WM 与新交互规范，GNOME 3 再来一轮。没有一家在日常小版本里动地基。

律八，生态件永远在核心稳定之后。i3bar 在 i3 诞生两年后（2011，4.0）才从内置条独立。sway 的锁屏、截屏、通知生态聚在 1.0（2019）前后。KDE 公告把 Hypertext Help System 排在中段，实际帮助系统成熟很晚。Xfce 的桌面图标 2007 年（4.4）才来。凡是「有了更好、没有也能用」的件，全部后置。

## 7. 对从零起步项目的三条提示

把八条律放到一个今天从零起步的桌面项目上，有三条直接可用。

第一，面板（任务栏）值得早立一个极简版。三大桌面环境的第一件桌面件家家是面板：KDE 的 kpanel 是作者亲自开工的第一件，Xfce 的第一版只有一根任务栏，GNOME 1.0 的门面是 panel。第一版面板可以小到只是一根只读窗口列表条，不做交互、不做 applet，i3 的内置工作区条先行两年、后来才独立成 i3bar 就是同款先例。它同时也是窗口件语义的第一块试金石：窗口增减、焦点变化这些语义有没有长对，面板一眼看得出来。

第二，配置读写库是基础设施，不是功能件，该在产生用户状态的件之前进仓。KConfig 是 KDE 第一批库类、格式仿 win.ini，早于几乎所有 App。桌面从图标摆位起就会产生用户状态（图标位置、窗口默认大小、终端字号），哪怕只是一个手写 ini 的读写器，也该在图标之前存在，免得各件各造存储格式。设置 GUI 完全不用急，律六说它天然晚到，Xfce 晚了十二年。

第三，生态件全部后置。帮助系统、桌面图标、托盘、通知这些「有了更好、没有也能用」的东西，历史先例里全部晚到（Xfce 的桌面图标距立项十一年），没有理由让它们先进。

还有一条关于顺序本身的。若项目的环境约束使窗口件必须先行（单进程、无可借的 WM、没有可寄居的模块协议），那走的是 WM 本体类项目的路径，与桌面环境类的面板先行相反，这是环境决定的合理选择。kwm 的故事给它的注脚是：别指望窗口件能最后做，面板语义会逼它提前进场。

## 启示

- 概念参考 KDE 的 FvwmModule 起步法。第一版桌面件寄居在现成基础设施上，等语义长大再自立，「先寄生、后自立」。
- 概念参考 KDE 的 xterm 套壳终端配方。先假源回显、再真 bash 走 PTY，就是这条配方拆成两步的版本。
- 概念参考 weston 的 shell 插件分层。桌面壳（面板、背景、任务切换）压在合成核心之上做层，桌面性后置，核心先稳。
- 概念参考 weston 的可装载后端。drm、wayland、x11、headless 皆为模块，显示后端做成可替换件，新增后端走同一形状。
- 概念参考 KDE 的 KConfig。极简配置读写库进第一批库件，格式可以土到 ini，但要先于产生用户状态的件存在。
- 概念参考 GNOME 的 Bonobo 教训。组件与进程边界的协议许诺得早、实现得晚，进程内接口先留、真进程后换，是这个教训的正面用法。
- 概念参考 i3 的内置工作区条。面板第一版可以小到一条只读窗口列表，两年后再分家也不迟。
- 概念参考 Xfce 的任务栏种子。桌面可以从一根任务栏长成完整环境，种子的「小」不是风险，「缺」才是。

## 参考来源

- https://kde.org/announcements/announcement — Ettrich 1996-10-14 原始公告全文。支撑第 2 节（Panel 定位、项目清单顺序、FvwmModule 路线、WM 「may come very last」、xterm 套壳终端）与第 6 节律二、律五。
- https://20years.kde.org/book/20yearsofKDE.pdf —《20 Years of KDE》。支撑第 2 节（Faure 的 kwm、kpanel、KConfig 回忆，Moore 的 kwm、kfm、kpanel 回忆，Gräßlin 的 KWin 1999 动工与 Ettrich 本不想写 WM）与第 6 节律三、律七。
- https://en.wikipedia.org/wiki/K_Desktop_Environment_1 — KDE Beta 1（1997-10-20）、KDE 1.0（1998-07-12）、1.1（1999-03-04）、kfm 与 kpanel 与 kwm 的对应关系。支撑第 2 节。
- https://en.wikipedia.org/wiki/KDE — KDE 2.0 发布日 2000-10-23。支撑第 2 节、第 6 节律七。
- https://lists.debian.org/debian-user/1997/08/msg02286.html — de Icaza 1997-08-28 转发的 GNOME 原始公告。支撑第 3 节（GTK 出自 GIMP、公告不提 WM、复用 KDE 代码、给 GNU 工具套前端）。
- https://en.wikipedia.org/wiki/GNOME_1 — GNOME 1.0（1999-03-03）构成、Enlightenment 为默认 WM、1.2（2000-05-25）、1.4（2001-04-02）与 Nautilus、Sawfish、Bonobo 延期。支撑第 3 节、第 6 节律三。
- https://ometer.com/projects — Havoc Pennington 项目页，Metacity 为 GNOME 2.x 默认 WM。支撑第 3 节。
- https://en.wikipedia.org/wiki/Metacity 与 https://en.wikipedia.org/wiki/GNOME — Metacity 条目原话「Before the introduction of Metacity in GNOME 2.2, GNOME used Enlightenment and then Sawfish as its window manager」，GNOME 主条目年表给 GNOME 2.2 发布于 2003 年 2 月。支撑第 3 节（Metacity 接棒版本与时间）。
- https://upload.wikimedia.org/wikipedia/commons/...（GNOME 1.0 截图文件名含 Panel 1 与 File Manager）与 http://www.linux.co.cr/（1999-02-25 GNOME 文件管理器新版本时间线）— gmc 在场的旁证。支撑第 3 节。
- https://en.wikipedia.org/wiki/Xfce — Xfce 全版本时间线（1996 立项、第一版只有任务栏、1998 Xfce 2 带 xfwm、1999-03 GTK 重写、4.0 2003-09-25、4.4 2007-01 Thunar 与桌面图标、4.6 2009-02 设置管理器）。支撑第 4 节、第 6 节律四律六律八。
- https://www.usenix.org/publications/library/proceedings/als00/2000papers/papers/full_papers/fourdan/fourdan.pdf — Fourdan 的 USENIX 2000 论文。支撑第 4 节（1997 初开工、首件是工具栏、接 fvwm 模块、xfwm 改自 fvwm、2000 年全家福清单）。
- https://man.archlinux.org/man/weston.1.en — weston(1) 手册。支撑第 5 节（合成器三合一、shell 插件、desktop shell 构成、后端模块清单）
- https://www.collabora.com/news-and-blog/news-and-events/weston-12-highlights-and-changes.html — libweston 分层。支撑第 5 节。
- https://docs.nvidia.com/jetson/archives/r36.4/DeveloperGuide/SD/WindowingSystems/WestonWayland.html — weston 为 Wayland 项目参考实现。支撑第 5 节。
- https://i3wm.org/downloads/ — i3 官方发布年表（3.α 2009-03-15、4.0 2011-07-31 等）。支撑第 5 节、第 6 节律八。
- https://raw.githubusercontent.com/i3/i3/4.0/RELEASE-NOTES-4.0 （经 GitHub API 读取 tag 4.0 同内容）— 「The internal workspace bar is obsolete. Use i3bar instead.」原句。支撑第 5 节。
- https://en.wikipedia.org/wiki/I3_(window_manager) 与 https://manpages.debian.org/trixie/i3-wm/i3.1.en.html — i3 从零用 C 写、libxcb、wmii 动机。支撑第 5 节。
- https://en.wikipedia.org/wiki/Sway_(window_manager) — sway 首发日 2016-03-24。支撑第 5 节。
- https://drewdevault.com/blog/Sway-1.0-released/ — sway 1.0（2019-03-11）、「1,315 days」、wlroots 与伴生生态。支撑第 5 节、第 6 节律七。
- https://drewdevault.com/blog/Why-rewrite-wlc （原文标题《Why did we replace wlc?》，2018-05-27）与 https://www.phoronix.com/news/Wlroots-Wayland-Compositor-Library （2017-12-28）— wlc 到 wlroots 的重写动机与时间。支撑第 5 节、第 6 节律七。
- https://en.wikipedia.org/wiki/LXDE 与 https://linux.fandom.com/wiki/LXDE — LXDE 2006 年以 PCManFM 为第一个模块起家。支撑第 6 节律二的反方向例证。
- https://en.wikipedia.org/wiki/Konsole 与 https://docs.kde.org/stable_kf6/en/konsole/konsole/konsole.pdf — Konsole 作者 Lars Doelle、版权起点 1997。支撑第 6 节律五。
