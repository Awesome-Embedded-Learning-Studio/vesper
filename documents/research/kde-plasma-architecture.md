# KDE Plasma 架构全景:分层、进程与演化

> 调研日期 2026-10-17 · 服务对象:vesper 手搓教学桌面 · 来源:网络检索加本地材料

## 1. 总览:一套自底向上的四层栈

KDE Plasma 不是单一程序。[Arch Wiki](https://wiki.archlinux.org/title/KDE) 把 KDE 软件项目概括为三块:桌面环境 Plasma、应用套件 KDE Applications、建立在 Qt 之上的附加库 KDE Frameworks。加上最底下的 Qt,整个栈是四层:

| 层 | 内容 | 回答的问题 |
|---|---|---|
| Qt | 跨平台工具包:部件、信号槽、事件循环、QML/Qt Quick | 控件怎么画、事件怎么派发 |
| KDE Frameworks | 70 个以上模块化库,源自原单体 kdelibs | 应用需要的通用能力:配置、IO、硬件、翻译、拼写 |
| Plasma Workspace / Desktop | 桌面壳:plasmashell、KWin、ksmserver、krunner | 桌面本体:窗口管理、面板、启动器、会话 |
| KDE Applications(Gear) | Dolphin、Kate、Konsole 等应用 | 用户每天用的程序 |

这四层的边界规则:Frameworks 只向下依赖 Qt 与其他 Framework,应用与壳在上层消费 Framework。Linux From Scratch 对 KF5 的定义可以当一句话总结:基于 Qt5 与 QML、从 KDE 4 单体库拆出的库集合([LFS BLFS](https://www.linuxfromscratch.org/blfs/view/11.1/kde/frameworks5.html))。

壳这一层自己又分成两个仓:plasma-workspace 装跨形态的通用件(会话管理、shell 本体、KRunner),plasma-desktop 装 PC 桌面特有的部分,官方仓库自述"desktop-specific parts of Plasma",而 Workspace 代码在 Desktop、Mobile 等形态间共用([GitHub plasma-desktop](https://github.com/KDE/plasma-desktop)、[Arch 包文件清单](https://archlinux.org/packages/extra/x86_64/plasma-workspace/files))。

对 vesper 而言,这张图先给一个定位参照:vesper 的 visor 约等于 Qt 那格(控件加渲染管线),vesper 本仓约等于 Workspace 那格,App 口之下的程序约等于 Applications 那格。Frameworks 那格 vesper 刻意空着,理由见 §6。

## 2. KDE Frameworks:单体 kdelibs 的拆解

### 2.1 拆法与 tier

KDE 4 时代所有共享库打成一个大包 kdelibs。2014 年它被拆成几十个独立发布的 Framework,动机是让 Qt 应用按需依赖最小子集,不必为用一个小功能背上整个 KDE 平台。首版 KF5 发布于 2014 年 7 月 7 日,官方发布文章说这是三年工程的结果,含"50 余个成熟、经过测试的框架"([dot.kde.org](https://dot.kde.org/2014/07/07/kde-frameworks-5-makes-kde-software-more-accessible-all-qt-developers/)、[kde.org](https://kde.org/announcements/frameworks/5/5.0.0/))。此后每月发版,大版本号跟随 Qt(KF5 配 Qt5,KF6 配 Qt6),KF6 随 2024 年 2 月 28 日的 MegaRelease 6 亮相([Wikipedia: KDE Frameworks](https://en.wikipedia.org/wiki/KDE_Frameworks))。

框架按依赖深度分层(维基百科记为 tier 1 到 4,另按运行时依赖分 Functional、Integration、Solutions 三类):

- tier 1 基本只依赖 Qt:KConfig、KCoreAddons、KI18n、Sonnet、Solid、Kirigami、KWindowSystem 等。
- tier 2 依赖 tier 1:KAuth、KCompletion、KCrash、KPty 等。
- tier 3 依赖复杂:KIO、KParts、KTextEditor、Plasma-framework、KService、KWallet 等。
- tier 4 基本是插件,应用开发者可忽略:Frameworkintegration、KHTML。

### 2.2 代表件逐个看

**KConfig** 是全栈用量最大的库之一。它把配置存成 INI 风格文件,分 KConfigCore(读写与默认值合并)与 KConfigGui(控件与配置文件的双向绑定)两半,还带 Kiosk 授权机制(KAuthorized),管理员可以禁掉个别动作,适合机房类场景([Wikipedia: KDE Frameworks](https://en.wikipedia.org/wiki/KDE_Frameworks))。

**KIO** 做网络透明的文件访问:应用面对统一的读写 API,底下由 ioslave 进程处理 http、ftp、sftp 等协议。把 IO 放进独立进程这件事从 KDE 2.0 起就是设计核心,当年官方发布词说 KIO"在独立进程中运行 IO,保证 GUI 不阻塞"([kde.org KDE 2.0 公告](https://kde.org/announcements/1-2-3/2.0/))。今天 Dolphin 与 KDE 文件对话框仍走 KIO([Wikipedia](https://en.wikipedia.org/wiki/KDE_Frameworks))。

**Solid** 是硬件抽象:设备发现、电源、网络、屏幕(KScreen)都从这一个 API 查,应用不接触 udev 一类的系统细节。KDE 4.0 公告对它的定位是"硬件集成框架,管理固定与可移动设备、电源管理、网络与蓝牙"([kde.org KDE 4.0 公告](https://kde.org/announcements/4/4.0/))。

**KI18n** 在 GNU gettext 外面包了一层:参数捕获、可定制标记、翻译脚本。**Sonnet** 是插件式拼写检查,后端可接 Hunspell、ASpell、HSpell、Enchant,带自动语言检测([Wikipedia](https://en.wikipedia.org/wiki/KDE_Frameworks))。

**KParts** 是组件嵌入框架:一个应用可以把另一个应用当部件嵌进来,Konqueror 嵌 Okular 的文档视图、Kate 与 Konqueror 嵌 Konsole 终端是经典用例。**KWindowSystem** 封装窗口系统信息,X11 侧基于 NETRootInfo/NETWinInfo(EWMH 属性),维基百科明说它是"KWin 与任务栏等图形壳部件的地基"([Wikipedia](https://en.wikipedia.org/wiki/KDE_Frameworks))。**Plasma-framework** 则是壳的概念库:libplasma、数据引擎、applet 装载与 QML 组件。

## 3. 进程模型:plasmashell 与 KWin 的分工

### 3.1 plasmashell:壳,但不管窗口

plasmashell 是一个进程,管面板、桌面、壁纸和桌面小物件。它的内部概念模型在 KDE 社区架构页([community.kde.org/Plasma/Architecture](https://community.kde.org/Plasma/Architecture),页面成文于 2010 年,概念沿用至今):

- **Plasmoid = Applet + DataEngine**。Applet 是可见可交互的那半,DataEngine 是不可见的数据源。一个 DataEngine 可以喂多个 Applet,页面举的例子是模拟时钟与数字时钟共用同一个时间引擎。
- **Containment 是容器**。Panel 本身就是一个 Applet,里面再装启动器、任务管理器、时钟。Desktop 也是 Containment,装 Plasmoid 和 Panel,并且可以按显示器、按活动存在多份,不再有 KDE 3 那种全局唯一的"桌面"。
- **Runner 是任务向插件**,KRunner 对话框(Alt+F2)靠它把一次输入分发给启动器、计算器、书签搜索等一堆插件。
- **ScriptEngine** 让 Applet、DataEngine、Runner 都可以用脚本语言写。

今天的 plasmoid 基本用 QML 写,在 plasmashell 进程内运行,教程与官方文档均以 QML 为默认路径([Plasma 6 applet 教程](https://medium.com/@dhruv8sh_34505/write-an-applet-for-plasma-6-0b8fd3a0334f)、[社区上手文](https://ilmanzo.github.io/post/your-first-plasma-6-widget))。

### 3.2 KWin:窗口管理器加合成器

KWin 是另一个进程,官方自述"易用而灵活的 Wayland 合成器,Linux 上主要随 Plasma 桌面使用"([GitHub kwin](https://github.com/KDE/kwin))。它管窗口堆叠、焦点、移动缩放、放置、虚拟桌面,以及合成。X11 下它是标准 X 窗口管理器。Wayland 下它就是显示服务器本身,合成、输入、屏幕管理一并承担。壳从不越过它去碰窗口:任务管理器取窗口列表走 KWindowSystem(X11 下读 EWMH 属性,Wayland 下走 KDE 私有协议,见 §3.4)。

plasma-workspace 里还有几个常驻进程:ksmserver 实现 XSMP 会话管理协议([仓库 README](https://github.com/KDE/plasma-workspace/blob/master/ksmserver/README)),krunner 作为独立进程承担搜索启动器,startplasma 脚本负责会话点火([Arch 包文件清单](https://archlinux.org/packages/extra/x86_64/plasma-workspace/files))。整套组件现在由 systemd 用户单元拉起,KDE 另有一个 plasma-systemd-integration 仓,提供 org.kde.plasmashell、org.kde.KWin 等 D-Bus 激活服务文件([GitHub plasma-systemd-integration](https://github.com/KDE/plasma-systemd-integration/blob/master/dbus/services/org.kde.plasmashell.service))。

这个分工的教学价值在于容错:plasmashell 崩溃或重启,窗口一个不丢,因为窗口的属主是 KWin。反向也成立,X11 时代 KWin 甚至可以整个换成 i3 或 bspwm,KDE 用户级文档就有替换教程([KDE UserBase 讨论](https://discuss.kde.org/t/kde-plasma-support-for-replacement-to-kwin/26605)),Wayland 下合成器成了显示服务器,实际不可替换,社区讨论明说只有 X11 会话能这么换([Reddit](https://www.reddit.com/r/kde/comments/1b3p8te/how_to_send_arguments_to_kwin_script_through_dbus/))。

### 3.3 D-Bus:进程之间的总线

KDE 2.0 自造过一套 IPC,叫 DCOP,建在 X11 的 ICE 库上([kde.org KDE 2.0 公告](https://kde.org/announcements/1-2-3/2.0/))。2006 年 KDE 宣布放弃 DCOP 改用 freedesktop.org 的 D-Bus,动因之一是 DCOP 只有会话总线,D-Bus 还有系统总线,且 Qt 4 起原生集成 D-Bus 绑定([OSNews: KDE Says Goodbye to DCOP](https://www.osnews.com/story/14768/kde-says-goodbye-to-dcop)、[Qt D-Bus 概览](https://doc.qt.io/qt-6/qtdbus-index.html))。D-Bus 的设计本身就大量借鉴 DCOP。

今天 plasmashell 与 KWin 各自在会话总线上挂服务:

- plasmashell 注册 `org.kde.plasmashell`,对象路径 `/PlasmaShell` 上的 `org.kde.PlasmaShell` 接口提供 `editMode` 属性、`evaluateScript`(执行 Plasma 脚本)和 `wallpaper`/`setWallpaper`(按屏幕号读写壁纸插件与配置),接口定义就在仓库 `shell/dbus/org.kde.PlasmaShell.xml`([plasma-workspace 源码](https://github.com/KDE/plasma-workspace/blob/master/shell/dbus/org.kde.PlasmaShell.xml))。
- KWin 注册 `org.kde.KWin`,对象路径 `/KWin`,是窗口管理与合成器的对外操作面,脚本装载也经 D-Bus([kwin 源码 dbusinterface.h](https://invent.kde.org/plasma/kwin/-/blob/master/src/dbusinterface.h))。

### 3.4 Wayland 下的第二条通道

Wayland 上 plasmashell 变成合成器的普通客户端,壳与合成器之间除了常规协议外,还有一组 KDE 私有协议。`kde-plasma-shell` 协议的说明写明"供 KF5 系 Wayland 壳与合成器通信,只能绑定一次",是桌面环境的实现细节而非普通客户端该用的东西([wayland.app](https://wayland.app/protocols/kde-plasma-shell)):

- `org_kde_plasma_shell.get_surface` 把一个普通 surface 升格为 plasma surface。
- `org_kde_plasma_surface` 上设角色与位置:`set_role`(normal、desktop、panel、notification、tooltip、criticalnotification、appletpopup 等,只许设一次)、`set_position`、面板自动隐藏的请求与事件。面板行为设定在 Plasma 6 中已标记弃用,改用 layer shell。
- 配套的 `kde-plasma-window-management` 协议把窗口列表与状态递给任务管理器([wayland.app 协议页](https://wayland.app/protocols/kde-plasma-window-management))。

### 3.5 配置与状态存储

全部配置是 XDG 目录下的 INI 风格文本文件,系统默认值与用户覆盖级联([Arch Wiki: KDE](https://wiki.archlinux.org/title/KDE)):

| 文件(均在 `~/.config`) | 管什么 |
|---|---|
| `plasma-org.kde.plasma.desktop-appletsrc` | 整棵容器树:面板、桌面、每个 applet 的位置与各自配置段 |
| `kwinrc` | 窗口管理器与合成器设置 |
| `kdeglobals` | 全局应用设置(颜色、字体等) |
| `plasmashellrc`、`plasmarc` 等 | 壳自身杂项 |

appletsrc 的结构值得注意:容器树按 `[Containments][N][Applets][M]` 的组路径编进同一个文件,QML 小物件经 `plasmoid.configuration.*` 读写自己的段([develop.kde.org 部件配置文档](https://develop.kde.org/docs/plasma/widget/configuration))。布局即数据,一个文件装下。

## 4. KWin 内部:装饰、特效、脚本、调度

### 4.1 窗口装饰:从 KDecoration 到 KDecoration2

X11 窗口管理器要给客户窗口包一个框,重定父(re-parent)成带标题栏的帧。KDecoration 就是做这个框的插件库,定位是"给一个会重定父 Client 的 X11 窗口管理器用"([社区 wiki](https://community.kde.org/KWin/Decoration))。这套老 API 后来被社区自己评价为"陈旧、受限、不是为合成场景设计的",于是有了 KDecoration2:装饰以插件形式提供,实现库里的 `Decoration` 类,与 KWin 之间经一个桥接口互相调用,现在的库独立成仓 kdecoration([invent.kde.org/plasma/kdecoration](https://invent.kde.org/plasma/kdecoration))。

写装饰实际有三条路:C++ 写 KDecoration 原生插件,或者用 Aurorae 引擎配 QML 主题,后者是绝大多数主题作者走的路。KDecoration2 时代 Gräßlin 专门写过主题引擎的一等公民化:插件在 `org.kde.kdecoration2` 元数据里声明自己支持主题,框架把选中的主题名递给插件,配置模块不再需要认识 Aurorae 的私货([Gräßlin 2014 博客](https://blog.martin-graesslin.com/blog/2014/10/window-decoration-themes-in-kdecoration2/)、[2012 博客](https://blog.martin-graesslin.com/blog/2012/07/qml-support-for-window-decorations/))。Aurorae 近年仍在重写,目标是消掉窗口缩放时的性能问题([Zahorodnii 博客](https://blog.vladzahorodnii.com/))。

### 4.2 特效:三段绘制钩子

KWin 合成器的绘制分三段:pre-paint、paint、post-paint,先对整屏跑一遍,再对每个窗口跑一遍,特效通过 EffectsHandler 接口挂进这些阶段,改窗口几何、透明度、变换。这套机制 Gräßlin 在 2009 年的特效教程里讲得最直白,含 `KWIN_EFFECT` 宏注册样例([Gräßlin 2009 博客](https://blog.martin-graesslin.com/blog/2009/07/how-to-write-a-kwin-effect))。现代文档已把特效搬到 QML 声明式写法,提供 ScreenEdgeHandler 一类组件监听屏幕边缘([develop.kde.org: KWin Effects](https://develop.kde.org/docs/plasma/kwineffect/))。

### 4.3 脚本接口

KWin 脚本用 JavaScript 或 QML 写,拿到两个全局对象:`workspace`(窗口管理器内核的操作面:窗口列表、信号、窗口管理方法)和 `options`(只读的当前配置)。事件挂接靠信号,如 `clientAdded`、`clientMaximizeSet`。脚本按 KPackage 格式打包(metadata.json 加 contents/code/main.js 或 main.qml),可带用户配置,`readConfig(key, default)` 读取([develop.kde.org: KWin 脚本教程](https://develop.kde.org/docs/plasma/kwin/))。

### 4.4 合成调度

合成器何时画一帧,KWin 换过方案:旧方案靠定时器驱动,新方案引入 RenderLoop 类,由呈现反馈在合适时机通知合成器开画,改善帧节奏与延迟([Zahorodnii 2020 博客](https://blog.vladzahorodnii.com/2020/12/10/compositing-scheduling-in-kwin-past-now-and-future))。

## 5. 演化史:从 KWM 到 Plasma 6

### 5.1 年表

| 时间 | 事件 | 架构含义 |
|---|---|---|
| 1996-10-14 | Matthias Ettrich 发起项目([Wikipedia: KDE](https://en.wikipedia.org/wiki/KDE)) | 目标是统一易用的桌面 |
| 1998-07-12 | KDE 1.0 发布 | kdebase 三件套:KWM(窗口管理器)、KPanel(面板)、kfm(文件管理器)([Linux Journal 评测](https://www.linuxjournal.com/article/3111)) |
| 2000-10-23 | KDE 2.0 | 推倒重来第一回:kwin 换掉 KWM 并遵循新窗口管理规范,KIO 独立进程网络透明,DCOP 做 IPC,KParts 做嵌入,Konqueror 接替 kfm,另有 aRts 与 XML GUI([kde.org 公告](https://kde.org/announcements/1-2-3/2.0/)) |
| 2002-04-03 | KDE 3.0 | 增量演进,kwin 改进 EWMH/ICCCM 支持([Wikipedia: KWin](https://en.wikipedia.org/wiki/KWin)) |
| 2008-01-11 | KDE 4.0 | 推倒重来第二回:Qt4、Plasma 壳(接替 kicker 与 kdesktop)、KWin 合成特效、Solid、Phonon、Oxygen,同期还有语义桌面 Nepomuk([kde.org 公告](https://kde.org/announcements/4/4.0/)、[W3C 案例](https://www.w3.org/2001/sw/sweo/public/UseCases/Nepomuk/)) |
| 2009-11-24 | 更名 | KDE 拆称 Plasma Workspaces、KDE Applications、KDE Platform,合称 KDE SC 4([Wikipedia: KDE](https://en.wikipedia.org/wiki/KDE)) |
| 2014-07 | KF5 与 Plasma 5.0 | kdelibs 单体拆成 50 余个框架,SC 捆绑发布终止,三产品各自发版([dot.kde.org](https://dot.kde.org/2014/07/07/kde-frameworks-5-makes-kde-software-more-accessible-all-qt-developers/)) |
| 2015(Plasma 5.4) | 首个可用 Wayland 会话 | KWin 自此能管理 Wayland 客户端([community wiki: KWin/Wayland](https://community.kde.org/KWin/Wayland)) |
| 2018-02(KWin 5.12) | X11 侧功能冻结 | 维护者明说 Wayland 后端加新功能容易,X11 太费工([Wikipedia: KWin](https://en.wikipedia.org/wiki/KWin)) |
| 2021 | Fedora 34 起 Plasma 默认 Wayland 会话 | 发行版层面灰度([Fedora 变更页](https://fedoraproject.org/wiki/Changes/WaylandByDefaultForPlasma)) |
| 2024-02-28 | Plasma 6 | Qt6 加 Frameworks 6,Wayland 会话成默认,悬浮面板为默认形态,打开文件改双击,发版节奏改为六个月([kde.org MegaRelease 6](https://kde.org/announcements/megarelease/6)) |

### 5.2 两次推倒重来与一次拆解

KDE 2.0 与 KDE 4.0 是真正的重写级转向。KDE 2.0 把整个底座换掉(IPC、IO、组件模型、窗口管理器全换),代价由当时的较小用户群消化。KDE 4.0 则是教科书级教训:发布词只讲新能力,只字未提目标受众与未完成项([kde.org 4.0 公告](https://kde.org/announcements/4/4.0/)页面确实没有任何保留措辞),而用户拿 3.5 的稳定预期去接一个过渡版本。OSNews 评测说它"带来的新 bug 几乎和新功能一样多"([OSNews](https://www.osnews.com/story/19192/review-kde-400)),Ars Technica 复盘说那是一次"传递得很差、接收得也很差"的混合信息([Ars Technica](https://arstechnica.com/information-technology/2008/07/kde-40-one-year-later/)),开发者则归因于期望值被抬高([ZDNet](https://www.zdnet.com/article/developers-respond-to-kde-4-backlash/)),社区一度出现 fork 呼声([Datamation 2012 回顾](https://www.datamation.com/open-source/desktop-linux-revolt-how-kde-survived-its-user-backlash.html))。Nepomuk 这条线也在这里折戟:2014 年 KDE Applications 4.13 用 Baloo 接替它,迁移到 KF5 时未获移植([KDE UserBase: Nepomuk](https://userbase.kde.org/Nepomuk))。

2014 年的 kdelibs 拆解性质不同:不是推倒,是把已验证的代码按依赖切开后各自发版。三年投入换来两个回报:Qt 生态的第三方应用开始直接消费 KDE 的库,以及 Qt 大版本迁移(KF5 到 KF6)时库可以整体并行编译过渡。

Wayland 迁移是第三种形态:进程边界不动(KWin 还是 KWin),内部整个重写。从 2012 年 Gräßlin 博客说"没有时间表"([原文](https://blog.martin-graesslin.com/blog/2012/09/a-real-update-on-the-progress-of-wayland-in-kwin-and-kde/)),到 5.4 首个会话、5.12 X11 冻结、2024 年默认切换,十二年长跑。能这样长跑的前提是应用只跟协议打交道,从不 include KWin 的头文件,壳内部怎么重写都与它们无关。

### 5.3 教训小结

第一,重写可以,一次性切默认值不行。Plasma 6 同样是 Qt 大版本加显示协议切换,策略是新旧栈并行安装、发行版先行灰度、默认值最后才动,没有重演 4.0 的舆情。第二,库与壳分层、库按依赖分 tier,是单体长大后唯一可持续的形状。第三,稳定的跨进程协议比任何内部 API 都长寿,KWin 靠 X11 与 Wayland 协议活过了三次内部重写。第四,进程边界即容错边界,plasmashell 崩了桌面还在,这个隔离是 Plasma 用十几年稳定性换来的共识。

## 6. 对 vesper 的启示

- 概念参考 KDE Plasma 的 Containment 容器。桌面与面板是同一种容器,差别只在摆放策略,Panel 自己也是 Applet。vesper 的 D2 Desktop 容器与将来的任务栏可以统一为一种容器抽象,容器嵌容器也正好回答 `Widget::kMaxChildren = 8` 的容量问题:桌面之下摆图标栏分组,而不是一张无限扁表。
- 概念参考 KDE 的 DataEngine 数据源。数据引擎与展示分离,一个时间引擎喂两个时钟。vesper 终端从 D3 的回环假源到 D4 的 PTY 源,走的就是这条线的微缩版,App 面对的应是窄数据源接口,实现随便换,与交接文档"递数据不共享状态"的拍板同构。
- 概念参考 KWin 的装饰桥。装饰是可替换插件,客户区像素从不归装饰管。vesper 的 D1 WindowFrame 从第一天就把标题栏当独立装饰对象建模:装饰与内容各报各的脏区,互不画进对方地盘,将来改标题栏样式不动内容渲染。
- 概念参考 KDE 的 appletsrc 序列化。整棵容器树一个 INI 文件,applet 各存各段,组路径即树路径。vesper 桌面布局(图标位置、窗口摆位)落成文件的形状可以照这个思路:布局即数据,一个文件装下,新增 applet 类型不改文件格式。
- 概念参考 KWin 脚本的 workspace/options 双入口。把窗口管理器的全部能力收敛成一个操作面加一个只读配置面,是脚本化的全部前提。vesper 若在教学上想暴露窗口管理能力,先把这个接口形状收敛好,单进程期它是 C++ 接口,D6 后同一形状换 IPC,顺序与 handover 文档从 D0 起立进程内边界的拍板一致。
- KDE 的规模才需要的,vesper 明确不抄:80 库的 tier 分层、KI18n、Solid 硬件抽象、KParts 嵌入、主题引擎与特效体系、语义桌面。Mono1 单色渲染下主题抽象没有对象,圆角画法 visor 已有。另有一条反面参照:Nepomuk 这类超前的支柱投入,下场是被 Baloo 接替并在下个大版本弃掉,教学仓更要按当期需要长件。

## 7. 参考来源

| URL | 支撑 |
|---|---|
| https://wiki.archlinux.org/title/KDE | §1 三层概括,§3.5 配置文件位置 |
| https://www.linuxfromscratch.org/blfs/view/11.1/kde/frameworks5.html | §1 KF5 定义 |
| https://github.com/KDE/plasma-desktop | §1 workspace 与 desktop 分包 |
| https://archlinux.org/packages/extra/x86_64/plasma-workspace/files | §1、§3.2 plasma-workspace 内容清单 |
| https://en.wikipedia.org/wiki/KDE_Frameworks | §2 tier 体系与代表件,§5 版本史 |
| https://dot.kde.org/2014/07/07/kde-frameworks-5-makes-kde-software-more-accessible-all-qt-developers/ | §2 KF5 首发 |
| https://kde.org/announcements/frameworks/5/5.0.0/ | §2 KF5 发布日期 |
| https://kde.org/announcements/1-2-3/2.0/ | §2 KIO 独立进程,§3.3 DCOP,§5 KDE 2.0 年表 |
| https://kde.org/announcements/4/4.0/ | §2 Solid 定位,§5 KDE 4.0 内容与措辞 |
| https://www.w3.org/2001/sw/sweo/public/UseCases/Nepomuk/ | §5 Nepomuk 案例 |
| https://userbase.kde.org/Nepomuk | §5 Baloo 接替时间 |
| https://community.kde.org/Plasma/Architecture | §3.1 Containment/Applet/DataEngine/Runner |
| https://medium.com/@dhruv8sh_34505/write-an-applet-for-plasma-6-0b8fd3a0334f | §3.1 QML plasmoid 现状 |
| https://ilmanzo.github.io/post/your-first-plasma-6-widget | §3.1 plasmoid 与 plasmashell 关系 |
| https://github.com/KDE/plasma-workspace/blob/master/ksmserver/README | §3.2 ksmserver 与 XSMP |
| https://github.com/KDE/plasma-systemd-integration/blob/master/dbus/services/org.kde.plasmashell.service | §3.2 systemd 与 D-Bus 激活 |
| https://github.com/KDE/kwin | §3.2 KWin 定位 |
| https://discuss.kde.org/t/kde-plasma-support-for-replacement-to-kwin/26605 | §3.2 KWin 可替换性 |
| https://www.osnews.com/story/14768/kde-says-goodbye-to-dcop | §3.3 DCOP 退场 |
| https://doc.qt.io/qt-6/qtdbus-index.html | §3.3 Qt 集成 D-Bus |
| https://github.com/KDE/plasma-workspace/blob/master/shell/dbus/org.kde.PlasmaShell.xml | §3.3 org.kde.PlasmaShell 接口 |
| https://invent.kde.org/plasma/kwin/-/blob/master/src/dbusinterface.h | §3.3 org.kde.KWin 对象 |
| https://wayland.app/protocols/kde-plasma-shell | §3.4 KDE 私有 Wayland 协议 |
| https://wayland.app/protocols/kde-plasma-window-management | §3.4 任务栏窗口列表协议 |
| https://develop.kde.org/docs/plasma/widget/configuration | §3.5 appletsrc 与 plasmoid.configuration |
| https://community.kde.org/KWin/Decoration | §4.1 KDecoration 旧 API 评价 |
| https://invent.kde.org/plasma/kdecoration | §4.1 KDecoration2 仓库 |
| https://blog.martin-graesslin.com/blog/2012/07/qml-support-for-window-decorations/ | §4.1 装饰两条路 |
| https://blog.martin-graesslin.com/blog/2014/10/window-decoration-themes-in-kdecoration2/ | §4.1 主题引擎一等公民 |
| https://blog.vladzahorodnii.com/ | §4.1 Aurorae 重写 |
| https://blog.martin-graesslin.com/blog/2009/07/how-to-write-a-kwin-effect | §4.2 三段绘制 |
| https://develop.kde.org/docs/plasma/kwineffect/ | §4.2 现代 QML 特效 |
| https://develop.kde.org/docs/plasma/kwin/ | §4.3 脚本接口 |
| https://blog.vladzahorodnii.com/2020/12/10/compositing-scheduling-in-kwin-past-now-and-future | §4.4 RenderLoop |
| https://en.wikipedia.org/wiki/KWin | §5 KWin 版本年表 |
| https://en.wikipedia.org/wiki/KDE | §5 项目年表与 2009 更名 |
| https://www.linuxjournal.com/article/3111 | §5 KDE 1.0 组件 |
| https://www.osnews.com/story/19192/review-kde-400 | §5 4.0 舆情 |
| https://arstechnica.com/information-technology/2008/07/kde-40-one-year-later/ | §5 4.0 沟通复盘 |
| https://www.zdnet.com/article/developers-respond-to-kde-4-backlash/ | §5 开发者回应 |
| https://www.datamation.com/open-source/desktop-linux-revolt-how-kde-survived-its-user-backlash.html | §5 4.0 后续 |
| https://blog.martin-graesslin.com/blog/2012/09/a-real-update-on-the-progress-of-wayland-in-kwin-and-kde/ | §5 Wayland 起步 |
| https://community.kde.org/KWin/Wayland | §5 5.4 首个 Wayland 会话 |
| https://fedoraproject.org/wiki/Changes/WaylandByDefaultForPlasma | §5 Fedora 灰度 |
| https://kde.org/announcements/megarelease/6 | §5 Plasma 6 内容 |
