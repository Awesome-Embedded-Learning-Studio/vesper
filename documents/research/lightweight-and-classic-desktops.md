# 轻量桌面与经典桌面对照：Xfce、LXQt、Enlightenment 与 Windows 95、classic Mac OS、CDE

> 调研日期 2026-10-17 · 背景：手搓教学桌面项目的立项调研 · 来源：网络检索加本地材料

## 1. 这份对照要回答什么

一个要从零长出「桌面本体加一组 App」的项目，开工前需要一份概念件清单：一个能叫「桌面」的东西至少由哪几件组成，轻量派是怎么用最少件数凑齐的，经典桌面（Windows 95、classic Mac OS、CDE）又为每件概念留下了什么原始论据。本文调研三个 Linux 轻量桌面（Xfce、LXQt、Enlightenment）与三个经典桌面，最后合成一张「最小完整桌面」的概念件对照表。所有概念借用只借概念，视觉走自己的路，每条借用在文末留 reference 标签。

## 2. Xfce：联邦式轻量桌面

### 2.1 组件分工

Xfce 自我定位是「快而轻、仍然好看易用」（Wikipedia 原文 "aims to be fast and lightweight while still visually appealing and easy to use"，https://en.wikipedia.org/wiki/Xfce）。它的桌面本体由这几个各自独立成仓的进程拼成：

| 件 | 职责 | 出处 |
|---|---|---|
| xfwm4 | 窗口管理器，自带可选合成器（2005 年 4.2 起内建透明与阴影） | Wikipedia Xfce |
| xfdesktop | 画桌面背景（壁纸）与桌面图标，右键出应用程序菜单（root menu），中键出窗口清单菜单 | https://docs.xfce.org/xfce/xfdesktop/usage 与 Debian 手册页 https://manpages.debian.org/testing/xfdesktop4/xfdesktop.1.en.html |
| xfce4-panel | 任务栏，「高度可配置，带一排插件」（窗口清单、工作区切换、启动器、系统托盘都是插件） | https://docs.xfce.org |
| xfce4-session | 会话管理器，负责登录恢复与登出 | Wikipedia Xfce |
| Thunar | 文件管理器，4.4（2007）取代 Xffm，「为速度与低内存占用设计」 | Wikipedia Xfce |
| xfce4-settings / xfce4-appfinder / xfce4-terminal | 设置、应用查找器、终端（终端基于 GNOME 的 VTE 库） | Wikipedia Xfce |

Xfce 自己写的库全是薄胶水：libxfce4ui 是「Xfce 桌面的控件库」，libxfce4util 是「扩展库」，exo 是「应用库」，garcon 是「符合 freedesktop.org 规范的菜单库」（Wikipedia Xfce 组件表）。也就是说，窗口装饰、面板、桌面面、文件管理、会话这些桌面本体它自己写，控件层以下全部借。

### 2.2 砍掉了什么

按 Wikipedia 的性能一节，Xfce「省掉了 KDE Plasma 与 GNOME 里许多视觉上取悦眼球的东西（比如动画）」，以此在低端机上保持流畅。砍单可以列得更长：

- 不写工具箱，全押 GTK，所有 Xfce 应用都是 GTK 应用。
- 不写文件监听与设备管理，4.8（2011）起直接采用 GIO、udev、ConsoleKit、PolicyKit。
- 不写 D-Bus 实现，4.14（2019）换用 GNOME 的 GDBus。
- 不写终端仿真库，xfce4-terminal 用 VTE（与 GNOME Terminal 同底）。
- 不写媒体框架，播放器 Parole 与混音器都架在 GStreamer 上。
- 不写通知协议，xfce4-notifyd 只是 freedesktop.org 通知规范的一个实现。
- 不养自己的 Wayland 合成器。4.20（2024-12）几乎所有组件都支持 Wayland，但会话标注实验性，实际跑在 labwc 或 Wayfire 这类外部合成器上。自己的合成器 xfwl4 确有其仓：2026 年 1 月 19 日建于 Xfce GitLab，用 Rust 写（README 要求编译器 1.92 以上），仓库自述「重度开发中」（原文 heavily under development），许多 xfwm4 与桌面环境功能已实现但尚无发布版。截至 2026 年 10 月中旬，仓库累计 900 余次提交，已打 4.21.0 与 4.21.1 两个开发版标签，提交仍在高频进行（https://gitlab.xfce.org/xfce/xfwl4）。

### 2.3 凭什么还是完整桌面

凭「概念件一件不缺，件与件之间用 freedesktop.org 规范粘合」。窗口管理、任务栏、启动入口、桌面图标、文件管理、会话、设置、终端全部在册，任何一件都可以单独卸掉换成别家，桌面仍然成立。Wikipedia 的评价是 Xfce「高度模块化，众多软件仓库把组件拆成多个包」，设计上遵循「传统 Unix 的模块化与可复用哲学，以及对 freedesktop.org 标准的遵守」。轻不是靠少件，是靠每件都小、层下全借。

## 3. LXQt：把窗口管理器也移出件数

### 3.1 架构

LXQt 官方自我描述是「轻量 Qt 桌面环境」「带现代观感的经典桌面」，口号是「它不会挡你的路，不会挂起或拖慢系统」（https://lxqt-project.org/about/）。血统是 LXDE-Qt 与 Razor-qt 两个项目的合并。组件面（官方站 About 页）：pcmanfm-qt（文件管理器）、lxqt-panel（面板加插件）、lxqt-runner（启动器兼计算器）、QTerminal（终端）、lximage-qt、qps、screengrab、lxqt-archiver，外加会话与设置模块。

关键取舍在窗口管理器。官方特性表写明「X11 窗口管理器无关」与「支持七种 Wayland 合成器」（lxqt-project.org）。开发者在 GitHub 讨论里的原话是「Yes, LXQt is WM/Compositor-agnostic」（https://github.com/lxqt/lxqt/discussions/2570）。发行版的传统默认是 Openbox（X11）。Wayland 会话的默认合成器官方有明确口径：2.1.0（2024-11）起新增 lxqt-wayland-session 组件，Labwc、KWin、Wayfire、Hyprland、Sway、River、Niri 七种合成器由用户在会话设置的 Wayland Settings 里自选，官方未指定唯一默认，但 release notes 写「Labwc 提供最稳定的会话，非常轻量、干净、可配置」，组件 README 也写明未选合成器时回落启动 Labwc（https://lxqt-project.org/release/2024/11/05/release-lxqt-2-1-0 与 https://github.com/lxqt/lxqt-wayland-session）。

pcmanfm-qt 是「一物二职」的样板：文件管理器进程同时负责桌面图标与壁纸（LXQt 没有独立桌面进程）。它做文件操作走的是 GLib 而不是 Qt 自带的文件 API（https://github.com/lxqt/pcmanfm-qt 仓库自述）。当前版本 2.4.0（lxqt-project.org 首页）。

### 3.2 与 Xfce 的取舍差异

两家的差异不在「谁更轻」，在轻的方向：

| 维度 | Xfce | LXQt |
|---|---|---|
| 窗口管理器 | 自己养 xfwm4，窗口吸附、合成开关都进自家设置 | 不养，任何 WM 都行，代价是一切 WM 相关功能只能建议不能假设 |
| 依赖形状 | GTK 加一串小库（GLib、VTE、GStreamer 各自独立） | Qt 一个大单体（控件、网络抽象、文件对话框都在里面），桌面件数更少 |
| 桌面图标 | 独立进程 xfdesktop | 并进 pcmanfm-qt，少一个进程 |
| 工具箱立场 | 跟着 GTK 走（2019 年才完成 GTK3 迁移） | 跟着 Qt 走 |

Xfce 换来深度集成（WM 是自家的，桌面与 WM 可以互相迁就），LXQt 换来件数（WM 移出清单，桌面并进文件管理器）。走「自己全画」路线的项目要拿两家当反面参照：它们的轻建立在「底下有人」上，自己底下没有 GTK 也没有 Qt，只有自画的控件。

## 4. Enlightenment：自绘一切的另一极

### 4.1 历史与定位

Enlightenment 1997 年由 Carsten Haitzler 发起，先是纯窗口管理器（E16 时代就以「虚拟桌面比显示器大得多」与「几乎所有操作有键盘捷径」著称，Wikipedia https://en.wikipedia.org/wiki/Enlightenment_(window_manager)）。2000 年起为重写版 E17 备料，长出了一套自己的工具箱 EFL（Enlightenment Foundation Libraries，官方称约一百万行 C，并自称速度与体积优于 GTK+ 和 Qt，这是项目自己的说法，https://www.enlightenment.org/about）。E17 从 2000 年 12 月开发到 2012 年 12 月 21 日才发布，重写耗时约十二年，之后 E20（2015-12）加入完整 Wayland 支持，现行版本 0.27（2025-01）。E16 分支独立存活，至今仍在维护，版本号到过 1.0。

定位原文值得整句引用：它自称「桌面壳」（desktop shell），「提供操作桌面或笔记本所需的一切，但它不是完整的应用套件」（https://www.enlightenment.org/about）。它同时是 Tizen 系统的窗口管理器与合成器，这是「GUI 与操作系统内核距离很远」的一个现成证据：同一套桌面壳既能跑 Linux 发行版，也能跑手机系统。

### 4.2 独门货

- 模块系统。官方定义模块是「给窗口管理器本体加功能的可插拔代码块」，并明说「不加载任何模块时，Enlightenment 相当荒凉，缺一大堆功能」（https://www.enlightenment.org/about-enlightenment）。任务栏、系统托盘、网络管理、电池、平铺，全是模块。
- Shelf（搁架）。屏幕边上的面板，每屏可以放多条，条上摆 gadget（背光、温度传感器之类）。对照 Xfce 的 panel 是「一根主栏加插件」，E 的 shelf 是「任意多条、可绑到特定虚拟桌面」（Bodhi Linux 文档 https://www.bodhilinux.com/w/gadgets-modules-and-shelves）。
- 活页器（pager）。虚拟桌面缩略图「随内容变化实时更新」，可以用鼠标中键把窗口拖进别的桌面、重排桌面本身（官方 About 页）。
- Everything 启动器。官方描述是「通用『输入即搜索、启动、切换』工具」，一个入口同时干启动程序、找文件、切窗口三件事。
- EFM。内建文件管理器，带桌面图标与可移动存储管理，长在 WM 进程里，不是独立程序。
- 永远在合成。官方表述是「即使没有 GPU 也有合成（手写优化的软件渲染）」，并称有用户「几个月都没发现」自己跑的是软件回退。对只有 1bpp 软件渲染的项目，这是「软件渲染也能做桌面效果」的先例。

E 的代价写在历史里：十二年重写、上手陡（2020 年评测仍批评其桌面菜单过于复杂，Wikipedia E）。它证明了单进程吞下「WM 加合成加桌面加文件管理加启动器」可行，也证明了这条路的时间成本。

## 5. 经典桌面概念件

### 5.1 Windows 95

任务栏是可用性测试逼出来的。微软 Kent Sullivan 的设计回顾（Socket 3 转载，https://socket3.wordpress.com/2018/02/03/designing-windows-95s-user-interface）记录：Windows 3.1 用户把最小化的窗口当成「没了」，因为重复启动同一程序把内存耗光，四成中级用户干脆回避多任务，初学者平均要花九分半钟才能找到一个不在眼前的程序。第一版方案（把最小化图标换成更大的「盘子」）失败了，测试揭示真问题是「窗口不能时刻可见，用户看不到自己开着什么」。最终答案是「每个任务在任务栏有自己的条目，且任务栏压在所有窗口之上」。系统托盘（Tray）则是从 Cairo 团队继承的。（概念参考 Windows 95 的任务栏）

开始菜单是从一个被砍掉的「初学者专用壳」里捞出来的，捞的是「单击可达、高可见度、菜单式交互」三样，最终设计「整合了启动程序之外的功能，给用户一个单按钮的 UI 根基」（同上文）。启动程序之所以配得上一个专用入口，是因为那是「用户的头号任务」。（概念参考 Windows 95 的开始菜单）

Explorer 同时顶替了 Program Manager（壳）与 File Manager（文件管理）：Wikipedia File Explorer 一文的表述是这次更新「旨在用 Windows Explorer 取代 Windows 3.x 基于 Program Manager / File Manager 的壳」（https://en.wikipedia.org/wiki/File_Explorer）。桌面从此成为真容器，文件管理器兼任系统壳这一身份也延续至今。（概念参考 Windows 95 的 Explorer 壳）

Alt+Tab 的秩序是 Z 序。Raymond Chen 在 The Old New Thing 里写明「图标按窗口 Z 序排列」，切到哪个窗口哪个就升到 Z 序顶，Alt+Tab 沿清单往下走，Alt+Shift+Tab 往回走（https://devblogs.microsoft.com/oldnewthing/20031020-00/?p=42093）。哪些窗口有资格进清单的规则在另一篇（https://devblogs.microsoft.com/oldnewthing/20071008-00/?p=24863/）。从实现角度看，这是桌面壳里最便宜的一件：桌面本来就要维护 Z 序，任务切换只是把它枚举出来。（概念参考 Windows 95 的 Alt+Tab 任务切换）

### 5.2 classic Mac OS

全局菜单栏。整个屏幕只有一根菜单栏，属于当前活动应用。Bruce Tognazzini（Apple 界面元老）的经典解释是屏幕边缘「实际上无限深」：鼠标多快撞上边缘都会停在那里，所以贴边的目标按 Fitts 定律近似无限大，他估算顶边菜单栏的获取速度约为窗口内菜单栏的五倍（https://www.asktog.com/columns/022DesignedToGiveFitts.html）。代价是菜单永远不属于窗口，属于应用，窗口与菜单在空间上分离。（概念参考 classic Mac OS 的全局菜单栏）

Finder 是「所有 Mac 操作系统上默认的文件管理器与图形用户界面壳」，「负责启动其他应用，并整体管理文件、磁盘与网络卷」（Wikipedia https://en.wikipedia.org/wiki/Finder_(software)）。classic Finder 是空间式的：「打开新文件夹就开一个新窗口，Finder 窗口是锁定的，永远只显示一个文件夹的内容」。Steve Jobs 后来批评这个设计「生成一大堆窗口，你就成了清洁工」。System 7（1991）从 MultiFinder 收编了多数特性，原生多任务成形。空间式窗口在 1bpp、无动画的环境里几乎是免费的：一个文件夹一个窗口，位置就是记忆。（概念参考 classic Mac OS 的 Finder）

### 5.3 CDE

CDE（Common Desktop Environment）1993 年由 COSE 联盟推动成形，基于 Motif 控件箱，后来列入 UNIX 98 标准，2012 年 8 月以 LGPL 开源（Wikipedia https://en.wikipedia.org/wiki/Common_Desktop_Environment，开源项目页 https://sourceforge.net/p/cdesktopenv/wiki/Home/）。

Front Panel 是它的门面。Oracle 文档的定义：「常用控件与桌面服务的集合，在每个工作区都可用」。中央是工作区切换器，控件上还有子面板（subpanel）收纳相关控件（https://docs.oracle.com/cd/E19455-01/806-1362/owtocde-46/index.html）。（概念参考 CDE 的 Front Panel）

工作区语义值得细看。原文：「工作区像分开的屏幕或桌面。一次只能显示一个工作区，但所有工作区里的应用同时在跑」，默认四个，名字就叫 One 到 Four。窗口可以「占一个工作区或全部工作区」，在窗口框上按右键选。这个「占用列表」模型（窗口属于哪些工作区是一个可枚举属性）比「虚拟大桌面加视口」的模型好实现也好推理，对单进程起步的项目尤其便宜。（概念参考 CDE 的工作区）

## 6. 最小完整桌面：概念件清单与对照

把六个桌面摊开，一个「能叫桌面」的东西由这八件概念件构成：

1. 窗口管理（框架、焦点、Z 序、最小化还原）
2. 任务清单（回答「我开着什么」，载体是任务栏或任务切换器）
3. 程序启动器（开始菜单、runner、Front Panel 皆是）
4. 桌面面（壁纸、桌面图标、桌面右键菜单）
5. 文件管理器
6. 会话（登录、恢复、登出、电源动作）
7. 设置（外观、输入、显示）
8. 终端（Linux 桌面的事实标配，三个轻量桌面全都自带）

| 概念件 | Xfce | LXQt | Enlightenment | Windows 95 | classic Mac OS | CDE |
|---|---|---|---|---|---|---|
| 窗口管理 | xfwm4 | 外借（Openbox 等） | E 本体（兼合成器） | 系统内建 | 系统内建 | dtwm |
| 任务清单 | xfce4-panel 窗口清单插件 | lxqt-panel | 任务栏模块 | 任务栏 | Finder 窗口堆叠（无任务栏） | Front Panel 无任务清单，靠窗口列表 |
| 启动器 | 应用菜单加 appfinder | lxqt-runner | Everything | 开始菜单 | Finder 加 Apple 菜单 | Front Panel 控件加 Application Manager |
| 桌面面 | xfdesktop | pcmanfm-qt 兼任 | EFM（进程内） | Explorer 兼任 | Finder 兼任 | dtfile 兼任（推测，未查到独立桌面进程记载） |
| 文件管理器 | Thunar | pcmanfm-qt | EFM | Explorer | Finder | dtfile |
| 会话 | xfce4-session | lxqt-session | 内建 | 系统内建 | Finder 常驻 | dtsession |
| 设置 | xfce4-settings | lxqt 设置模块 | settings panel | 控制面板 | 控制板 | Style Manager |
| 终端 | xfce4-terminal | QTerminal | Terminology | 无（命令提示符不算） | 无（Terminal 是后加应用） | dtterm |

轻量派省件数的路数归纳成三条：

- 借件（Xfce）：桌面本体以下的层全部借现成的（工具箱、文件监听、终端仿真、媒体、D-Bus），自己只写概念件本身。件数不少，自写代码量少。
- 砍件（LXQt）：把窗口管理器移出自家清单，桌面面并进文件管理器，件数直接减二。
- 吞件（Enlightenment）：单进程吞下 WM、合成、桌面面、文件管理、启动器，件数最少，但必须先长出自己的工具箱。

经典侧的省法更古老：Windows 95 让文件管理器兼任壳，省掉独立的桌面进程（这条路 pcmanfm-qt 又走了一遍），classic Mac OS 用一根全局菜单栏顶掉每个窗口的菜单件，CDE 把启动器合进面板、用工作区把「屏幕不够大」变成「屏幕够多」。

再往下砍就不再是桌面：窗口管理加任务清单加启动器加桌面面是四件硬底线，其中桌面面与文件管理器可以合并，任务清单与启动器可以共用一个面板进程。终端严格说不是桌面本体的一部分，但三个轻量桌面全都把它当旗舰应用养，把终端排第一个 App 顺这个惯例。

## 7. 启示

- 任务栏的第一性论据是「看得见自己开着什么」，不是装饰。单色桌面没有动画没有预览，任务栏反而是最便宜的多任务答案：一个按钮列表加一个焦点状态就成立。（概念参考 Windows 95 的任务栏）
- Alt+Tab 是 Z 序的免费副产品。桌面壳本来就维护窗口叠放顺序，任务切换器只是枚举它。键盘焦点在单进程桌面里只是一个变量，比任何真实 WM 都简单。（概念参考 Windows 95 的 Alt+Tab 任务切换）
- 启动器可以后长，开始菜单的原始论据是「启动程序是头号任务」。桌面图标是最早的启动入口，runner 式的统一入口（输入即搜索启动切换）可以等 App 多了再上。（概念参考 Enlightenment 的 Everything 启动器）
- 工作区语义值得早期就定：窗口对工作区存「占用列表」（占一个或占全部），不搞无限虚拟大平面。单进程期这是一个数组属性，将来换进程也不动语义。（概念参考 CDE 的工作区）
- 桌面图标进程与文件管理器可以是同一个 App 模块。Explorer 与 pcmanfm-qt 都是这个身份合并的判例，文件管理器届时直接接管桌面面即可。（概念参考 LXQt 的 pcmanfm-qt）
- 一根全局菜单栏值得考虑：贴边目标按 Fitts 定律易点，单色下它是屏幕顶上一条固定条带，还省掉每个窗口画菜单的成本。但这与「不复制别人设计」的纪律有张力，只借「顶边一条全局命令区」的概念，形态自己定。（概念参考 classic Mac OS 的全局菜单栏）
- 软件渲染做效果有先例。Enlightenment 无 GPU 时手写优化的软件合成，用户数月察觉不到。1bpp 脏区条带重画与它是同一信仰的不同像素深度。（概念参考 Enlightenment 的软件合成）
- 终端自研有伴。Terminology 不用 VTE，自己写终端核心，手搓终端控件与它同路。（概念参考 Enlightenment 的 Terminology）
- 单进程桌面有先例也有代价。E 证明了单进程吞下全套桌面壳可行，代价是十二年重写。进程内接口先立、后换真进程，正是给这张代价单上的保险。

## 8. 参考来源

- https://docs.xfce.org （Xfce 官方文档门户，支撑 §2 组件分工）
- https://docs.xfce.org/xfce/xfdesktop/usage （xfdesktop 职责：壁纸、图标，支撑 §2）
- https://manpages.debian.org/testing/xfdesktop4/xfdesktop.1.en.html （xfdesktop 的 root menu 与中键窗口清单，支撑 §2）
- https://en.wikipedia.org/wiki/Xfce （组件表、胶水库、版本史、性能取向、Wayland 现状，支撑 §2、§6）
- https://gitlab.xfce.org/xfce/xfwl4 （xfwl4 仓库：2026-01-19 建仓、Rust、重度开发中无发布版、4.21.x 标签，支撑 §2.2）
- https://lxqt-project.org （LXQt 首页与版本 2.4.0，支撑 §3）
- https://lxqt-project.org/about/ （LXQt 定位、模块化、WM 无关、七种 Wayland 合成器，支撑 §3）
- https://github.com/lxqt/pcmanfm-qt （pcmanfm-qt 职责与 GLib 文件操作，支撑 §3）
- https://github.com/lxqt/lxqt/discussions/2570 （开发者原话 WM/Compositor-agnostic，支撑 §3）
- https://lxqt-project.org/release/2024/11/05/release-lxqt-2-1-0 （LXQt 2.1.0 release notes：lxqt-wayland-session、七种合成器、「Labwc 提供最稳定的会话」，支撑 §3）
- https://github.com/lxqt/lxqt-wayland-session （Wayland 会话组件 README：未选合成器时回落启动 Labwc，支撑 §3）
- https://www.enlightenment.org/about （E 定位、EFL 规模、Tizen，支撑 §4）
- https://www.enlightenment.org/about-enlightenment （模块、shelves、pager、Everything、EFM、软件合成，支撑 §4）
- https://en.wikipedia.org/wiki/Enlightenment_(window_manager) （E 的历史、E17 十二年、0.27 版本线，支撑 §4）
- https://www.bodhilinux.com/w/gadgets-modules-and-shelves （shelves 与虚拟桌面绑定，支撑 §4）
- https://socket3.wordpress.com/2018/02/03/designing-windows-95s-user-interface （Kent Sullivan 的 Windows 95 界面设计回顾，任务栏与开始菜单全部可用性数据，支撑 §5.1）
- https://www.ics.uci.edu/~kobsa/courses/ICS104/course-notes/Microsoft_WindowsGuidelines.pdf （1995 年 2 月版 Microsoft Windows 界面指南存档，支撑 §5.1）
- https://en.wikipedia.org/wiki/File_Explorer （Explorer 取代 Program Manager / File Manager 的表述，支撑 §5.1）
- https://devblogs.microsoft.com/oldnewthing/20031020-00/?p=42093 （Alt+Tab 顺序即 Z 序，支撑 §5.1）
- https://devblogs.microsoft.com/oldnewthing/20071008-00/?p=24863/ （哪些窗口进 Alt+Tab 清单，支撑 §5.1）
- https://en.wikipedia.org/wiki/Finder_(software) （Finder 的壳定位、空间式窗口、System 7 演进、Jobs 评价，支撑 §5.2）
- https://www.asktog.com/columns/022DesignedToGiveFitts.html （Tognazzini 论屏幕边缘与 Fitts 定律，支撑 §5.2）
- https://docs.oracle.com/cd/E19455-01/806-1362/owtocde-46/index.html （Oracle Solaris CDE 文档：Front Panel、工作区、子面板，支撑 §5.3）
- https://en.wikipedia.org/wiki/Common_Desktop_Environment （CDE 的 Motif 基础、UNIX 98、2012 年开源，支撑 §5.3）
- https://sourceforge.net/p/cdesktopenv/wiki/Home/ （CDE 开源项目页，支撑 §5.3）
