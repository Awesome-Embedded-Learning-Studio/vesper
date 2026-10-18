# GNOME 架构全景

> 调研日期 2026-10-17 · 服务对象：vesper 手搓教学桌面 · 来源：网络检索加本地材料

## 0. 为什么看 GNOME

vesper 的终局是「桌面本体加一组 App」，GNOME 是这条路上走得最远的开源样本。它把控件库、设计语言库、壳层、设置守护进程、应用名册、设计规范各立门户，每一层的进退都有十几年判例可查。本篇按五个问题组织材料：分层各自管什么，Shell 扩展怎么运转，出厂带哪些 App，HIG 怎么治理设计，以及 1.x 到 50 的演化史。重要事实均给来源 URL，查不到出处的明写。

## 1. 分层总览

GNOME 不是一个程序，是一条分工明确的栈。我把它画成一张表，自底向上看：

| 层 | 定位 | 运行形态 | 备注 |
|---|---|---|---|
| GTK 4 | 通用控件库，自带渲染与文本栈 | 应用链接的共享库 | 治理独立于 GNOME，任何桌面可用 |
| libadwaita | Adwaita 设计语言的代码实现 | GTK 4 之上的附加库 | GNOME 42 起随发布集出货 |
| Mutter | Wayland 合成器与窗口管理器 | 库（libmutter），被 gnome-shell 进程内嵌 | Wayland 下即显示服务器本体 |
| GNOME Shell | 壳层 UI 与交互 | 独立进程，以 Mutter 插件形式承载 | UI 主要用 JavaScript 写 |
| gnome-settings-daemon | 设置事务守护进程 | 独立进程，插件化 | 电源、媒体键、色彩、键盘布局等 |
| GDM | 登录管理器 | 独立进程 | greeter 是受限会话形态的 Shell |

两条 UI 栈并行是理解 GNOME 的第一道门。应用走 GTK 加 libadwaita，壳层走另一套：Mutter 自带 Clutter 场景图，其上叠一个叫 St 的壳层控件库（CSS 样式化的按钮、图标、输入框）。GNOME Shell 不用 GTK。[GJS 指南的架构页][gjs-arch]列举了 Shell 这条栈的能力：用 Mutter 控显示器、工作区与窗口，用 Clutter 与 St 造 UI 元素。Clutter 比 GTK 更抽象，偏场景图与动画原语，合成器内的壳层 UI 当初需要的就是这些。

Shell 进程本身就是显示服务器。Wayland 会话里 gnome-shell 是合成器本体，X11 会话里它是 Xorg 之上的合成窗口管理器。[Wikipedia 的 GNOME Shell 条目][wp-shell]把它记为「以 C 与 JavaScript 混写、作为 Mutter 插件运行」。[Linux Graphics Stack Book 的 GNOME 章][lgsb]说得更直白：Shell 几乎全用 JavaScript 写成，调用 libmutter、Clutter、St 与 Gio。GNOME 50（2026 年 3 月）起 X11 会话整体移除，X11 应用经 XWayland 继续跑，官方在 2025 年 6 月就发了 [X11 Session Removal FAQ][x11faq] 预告。

会话里还有一圈配套角色：GSettings 加 dconf 做设置存储，gnome-keyring 管密钥，xdg-desktop-portal-gnome 给沙箱应用做截图与文件访问代理。这些不是本篇重点，但「设置存储」与「设置执行」分离这件事，下文会反复遇到。

## 2. 各层职责细看

### 2.1 GTK：刻意去 GNOME 化的通用库

GTK 出身是 GIMP Toolkit。1997 年 GNOME 选它做地基，动机之一是 KDE 用的 Qt 在 2.0（1999 年 6 月）之前持专有许可，而 GTK 走 LGPL，闭源软件也能链接（[Wikipedia GNOME 条目][wp-gnome]）。

GTK 4 周期里 GNOME 做了一次明确切割：把 GNOME 专属控件与样式从 GTK 抽出，成立 libadwaita。tesk.page 的[分析文章][tesk]把这次切割概括为让 GTK 回归「所有桌面共用的基础」，代价是 GTK 4 移除了部分 GNOME 风格控件，这件事本身也引起过争议。对 vesper 来说，这一步的对照意义放在启示一节讲。

### 2.2 libadwaita：设计语言装进库里

Adwaita 这个名字来自梵语，意为独一无二。[Wikipedia 的 Adwaita 条目][wp-adwaita]把 libadwaita 记为 GNOME 官方设计语言的 GTK 4 实现。它提供 HIG 合规控件、自适应布局（窄屏自动变形）与预置样式，应用作者拿它就能得到一致的 GNOME 观感。GNOME 42（2022 年 3 月）起随发布集出货。

主题之争绕不开。libadwaita 对传统 GTK 主题机制关上了大部分门。[GNOME Discourse 的官方解释][adw-theming]说，从技术上讲它并没有阻止应用被主题化，但外部主题会破坏应用的自绘控件，出了问题 bug 单会砸到应用作者头上。2021 年这个立场公开后社区反弹不小，[OSNews 当时汇集了各方不满][osnews]。

从治理视角看，这一步把设计规范从「建议」变成了「默认强制」。想符合 HIG，最省力的路径就是直接用 libadwaita，规范与实现合流了。

### 2.3 GNOME Shell 与 Mutter：一个进程，两种语言

gnome-shell 进程内嵌 libmutter，Shell 以 Mutter 插件的形式存在。C 侧是 Mutter 的合成、输入与窗口管理，加上 St 控件库。JS 侧经 GJS（GNOME 的 JavaScript 绑定）与 GObject Introspection 调用这些 C 库，总览、顶栏、通知、锁屏这些壳层 UI 都在 JS 里写成。

Shell 自带的可复用模块值得点名：PanelMenu（顶栏指示器，Wi-Fi、蓝牙、电源都在用）、PopupMenu（菜单与子菜单）、ModalDialog（运行对话框与认证对话框的底座）。扩展机制正是踩在这些模块上长出来的（[GJS 指南][gjs-arch]）。

壳层与设置的分工一句话可以说清：Shell 管用户看得见的交互，gnome-settings-daemon 管系统侧的设置执行，两者经 D-Bus 各自对外。截屏是这条分界的一个注脚：GNOME 42 起独立 Screenshot 应用退位，功能收进 Shell 本体（见 [mcatanzaro 的 core apps 复盘][mcatanzaro]）。

### 2.4 gnome-settings-daemon：设置落地的地方

Debian 的包描述把它概括为「处理 GNOME 与 GTK/KDE 应用共享的设置」，覆盖键盘布局、无障碍、快捷键、媒体键等。它是插件化的守护进程。我从 [GitHub 镜像的 plugins/ 目录][gsd-plugins]清点了一遍，现役插件 17 个：a11y-settings、color、datetime、housekeeping、keyboard、media-keys、power、print-notifications、rfkill、screensaver-proxy、sharing、smartcard、sound、usb-protection、wacom、wwan、xsettings。

xsettings 插件是兼容桥：把 GSettings 里的字体与主题设置用 XSettings 协议广播给 X11 下的老式应用。媒体键、电源按钮行为、显示色彩管理这类「没有窗口的事」都归这个进程。GNOME 50 移除 X11 会话后，XWayland 场景下这类兼容职责如何收尾，官方 FAQ 只说应用侧不受影响（[FAQ][x11faq]）。

## 3. Shell 的 JS 扩展机制

### 3.1 机制怎么运转

分发形态是 zip，两个必备文件：metadata.json 与 extension.js。前者带全局唯一 uuid、名称和 shell-version 数组，后者是入口代码。安装位置有两处：用户级 ~/.local/share/gnome-shell/extensions/，系统级 /usr/share/gnome-shell/extensions/（[GJS 指南的 Anatomy 页][gjs-anatomy]）。

生命周期分三段。构造函数跑一次性初始化，审核规则明确禁止在这里改动 Shell、连接信号或添加事件源。enable() 与 disable() 反复成对调用，锁屏与解锁都会触发一对。指南强调 enable 里做的一切必须在 disable 里撤销，「不撤销是审核被拒最常见的原因」（[Anatomy 页][gjs-anatomy]）。

运行环境没有沙箱。[指南原话][gjs-arch]：扩展一旦加载启用，「effectively becomes a part of GNOME Shell」。它拿到的是 Shell 的全部内部对象，改顶栏、改总览、换窗口行为都行，也能 import 任何支持 GObject Introspection 的库。

几个细节值得记：

- session-modes 字段（GNOME 42 加入）声明扩展活在哪个会话形态：user、unlock-dialog（锁屏）、gdm（登录界面）。登录界面也是扩展宿主，这说明 GDM 的 greeter 就是一个受限模式的 Shell。
- 版本闸：shell-version 对不上就拒绝加载。开关 disable-extension-version-validation 在 GNOME 40 之前默认开（随便装），40 起默认关。
- GNOME 45（2023 年 9 月）把 GJS 自创的 import 系统换成标准 ES Modules，新旧语法互不兼容，存量扩展全体报废。[官方博客][e45]称这是换取标准合规的「teeny tiny drawback」，同时给了移植指南。

上架要走审核。extensions.gnome.org 人工审核，判据成文于 [Review Guidelines][gjs-review]（例如 init() 期间不得动 Shell，偏好设置进程禁止 import Clutter、Meta、St、Shell）。2025 年 12 月起审核明确拒绝 AI 生成的低质量扩展（[Linuxiac 报道][ext-ai]）。

### 3.2 生态的利

改桌面不用编译不用重启，装与开关即生效，覆盖面从顶栏小指示器到完整的 dock 替换。

它救过一次范式革命的场。Torvalds 2012 年评 GNOME 3.4「a total user experience design failure」，转投 Xfce，2013 年又回来了，ZDNet 报道他明说靠的是 Shell 扩展补齐了缺失的东西（[Wikipedia GNOME 3 条目][wp-gnome3]）。一个把核心体验推倒重来的壳层，靠扩展机制接住了流失的用户。

GNOME 自己也吃这碗饭：GNOME Classic 这个官方传统布局，本身就是一组官方扩展拼出来的（[Wikipedia GNOME][wp-gnome]）。

### 3.3 生态的弊

无兼容承诺。Shell 内部 API 随版本任意变动，半年一发的节奏里扩展作者要不断追赶。[Discourse 上「GNOME Shell breaks catastrophically every few months」一帖][disc-break]是常态样本，不是偶发牢骚。

无沙箱意味着无隔离的故障半径：扩展写崩了，整个桌面会话陪葬。

版本闸有副作用。不少扩展其实还能跑，只是 metadata 里的版本号没更新就被禁用，Fedora 与 GNOME 社区都有长期讨论（见上帖与检索摘要，未逐帖核原文）。

审核吞吐量有限。2025 年后 AI 生成的投稿潮把管道挤得更紧，官方只能加质量门槛（[Linuxiac][ext-ai]）。

## 4. Core Apps：出厂名册与它的治理

### 4.1 今天的名册

[apps.gnome.org][appsgnome] 把应用分三级：Core 覆盖桌面上的常规任务、通常预装，Circle 是第三方精品，Developer Tools 面向开发者。我按 2026 年 10 月的站点清点，Core 共 28 个，大半带 mobile 标记（可在 GNOME 移动设备运行）。按用途分组：

| 分组 | 应用（项目名） |
|---|---|
| 文件与文档 | Files（Nautilus）、Text Editor（gnome-text-editor）、Document Viewer（Papers）、Document Scanner、Logs |
| 系统管理 | Settings、Disks、Disk Usage Analyzer（Baobab）、System Monitor、Software |
| 终端与帮助 | Console、Help（Yelp）、Tour |
| 影音 | Image Viewer（Loupe）、Video Player（Showtime）、Audio Player（Decibels）、Music、Camera（Snapshot） |
| 网络与信息 | Web（Epiphany）、Connections、Maps、Weather |
| 个人事务 | Calendar、Contacts、Clocks、Calculator |
| 字体与字符 | Fonts、Characters |

几个定位值得注意。Files 是桌面身份的锚，从 Nautilus 时代一路续到今天。Console 与 Text Editor 都是 GNOME 41 到 42 间新写上位的应用，替换的对象（gnome-terminal 与 gedit）反而是更老更全的。Web（Epiphany）是 GNOME 自带的浏览器，定位轻量，与全功能浏览器错开。

### 4.2 名册怎么进出的

[mcatanzaro 2023 年的复盘][mcatanzaro]给了完整流水账，我抽关键节点：

- 3.22（2016）他主导大重组，是这段历史的起点。
- 3.26 进 Music、To Do、Document Scanner。3.28 因共识不足移除 To Do，此后 release team 与 design team 的沟通机制改进。
- 3.38（2020）立 Developer Tools 子类（Builder、dconf Editor 等），明说这些不打算默认安装。
- 41（2021）移除 file-roller（Files 自己能开压缩包），gedit 退位给新写的 Text Editor。
- 42（2022）移除 Screenshot（Shell 内置了），Terminal 退位给 Console。

Console 事件是名册治理的转折点。Fedora 认为 Console 功能不全拒绝跟进。mcatanzaro 复盘原话：「We messed up by adding the app to core before downstreams were comfortable with it」。由此立了 Incubator 孵化流程：想进 core 的应用先公告、先观察，给下游留缓冲。维护力量是另一道硬门槛，Photos 因缺活跃维护者在核心评审中出局。

这段历史给出两条成文标准：下游接受度先于功能完备，维护者断档比功能缺失更致命。

### 4.3 Core 之外的圈层

[GNOME Circle][circle] 是第三方应用的荣誉圈，申请审核制，通过者得 GNOME Foundation 会员、nightly Flatpak 构建与推广资源。[Phoronix 2025 年底的统计][phoronix-stats]：Core 应用 44% 用 C 写，Circle 应用里 Rust 占 41.7% 排第一，两个圈层的主流语言已经分叉。

2026 年 5 月 30 日 Circle 宣布暂停接收新申请，理由是审核积压与低质量 AI 生成应用泛滥。新政策管质量不管 AI 本身（[Linuxiac][circle-ai]、[Phoronix][circle-ai-px]）。一个二十多年的生态在 2025 到 2026 年被生成式工具推入审核危机，这个现象本身值得档案式记录。

## 5. HIG：设计怎么治理

现行 [GNOME HIG][hig] 的骨架有五块：Design principles（设计原则与目标）、Guidelines（文案、图标、应用识别、各类输入的通行约定）、Patterns（可组合的设计单元，按容器、导航、反馈、控件四类组织）、Reference（标准快捷键与颜色）、Resources（设计工具与资产）。Platform 定义一节明写 HIG 与 GTK 4 加 libadwaita 配套使用。改动走 GitLab issue，公开进行。

我把它的治理链条归纳为四环。第一环，设计团队集中提案，讨论在 GitLab 公开。第二环，定稿进 HIG 文档。第三环，libadwaita 把规范编成代码，用库即合规。第四环，审核管道把关：Circle 审应用，extensions.gnome.org 审扩展，判据都对接 HIG 与 Review Guidelines。文档、代码、审核三处对齐，这是 GNOME 设计一致性的来源。

HIG 自己也换代过。2020 年 1 月设计团队在 Discourse 提议重写，2021 年 5 月 Allan Day [发博客][aday-hig]预告新版将挂在临时地址，2021 年 8 月宣布转正、旧版退役。[The Register 报道][register-hig]转正的同时也记录了社区反对声。一份规范文档活了快二十年还在换代，说明它被当成活资产经营。

对自研桌面可学的治理方式，我总结三条。规范绑定控件库版本，避免文档与实现脱节。Pattern 分类比控件清单活得久，控件会换名字，模式不会。规范落地靠代码默认值与审核管道，光靠文档约束不住任何人。

## 6. 演化史：从 1.x 到 50

| 时间 | 事件 |
|---|---|
| 1997-08 | Miguel de Icaza 与 Federico Mena 动工。动机之一是 KDE 用的 Qt 到 2.0（1999-06）才离开专有许可，GTK 出身 GIMP 工具箱、走 LGPL |
| 1999-03 | GNOME 1.0 |
| 2002-06 | GNOME 2.0，GTK 2 之上重做，HIG 首次登场 |
| 2004-2010 | 2.x 稳态期。2.6（2004）试过 spatial 文件管理器，2.30 收回。Nautilus、Evince、Orca 在这个窗口长出来 |
| 2011-04 | GNOME 3.0：Shell 换掉传统面板，Metacity 换 Mutter，Clearlooks 换 Adwaita，交互隐喻转向 Activities Overview |
| 2012-2013 | 争议顶点，见 6.1 |
| 2014-2020 | 3.x 渐进修整，3.22（2016）core 应用名册重组，Wayland 支持逐年成熟 |
| 2021-03-24 | GNOME 40：总览改横向布局，版本号从 3.38 直接跳 40，此后单数字递增、半年一发 |
| 2021-08 | 新 HIG 转正 |
| 2022-03 | GNOME 42：libadwaita 随发布集出货，生态整体向 GTK 4 加 libadwaita 迁移 |
| 2023-09 | GNOME 45：扩展切 ES Modules。Sovereign Tech Fund 2023 到 2024 注资 100 万欧元，Wikipedia 记最大贡献者为 Red Hat |
| 2026-03-18 | GNOME 50：X11 会话与 Mutter 的 X11 后端移除，XWayland 保留，官方 FAQ 提前九个月预告 |
| 2026-09 | GNOME 51 按节奏接续（[Wikipedia 版本表][wp-gnome]） |

版本号的跳跃本身是个决策样本：3.38 之后直接 40，官方解释是改为半年一发、递增单个数字，因此永远没有 3.39 也没有 4.0。语义化版本对桌面发行节奏不友好，GNOME 干脆放弃治疗，这件事对发行方心智的简化超过对开发方的扰动。

### 6.1 GNOME 3 争议与三家回流

3.0 砍掉了 GNOME 2 的底面板任务栏与传统最小化习惯，转向极简窗口管理流（[Wikipedia GNOME][wp-gnome]）。发布当天 Ars Technica 预言用户反弹，ZDNet 的 Vaughan-Nichols 写过可用性倒退的评论（[Wikipedia GNOME 3][wp-gnome3]）。

退路给过，但是个残缺品。3.0 到 3.6 有 fallback mode，显卡不支持 OpenGL 时退回 GNOME Panel。Linux Magazine 的 Byfield 记录它默认没有 applet、没有桌面右键菜单、没有图标。3.8 移除 fallback，换成官方扩展拼出的 GNOME Classic，GNOME Panel 本体存续于后来的 Flashback 会话（[Wikipedia GNOME][wp-gnome]、[Dedoimedo 当年评测][dedoimedo]）。

MATE 走的是硬分家。2011 年 6 月 18 日，阿根廷 Arch Linux 用户 Perberos 在 Arch 论坛宣布 fork GNOME 2，8 月 19 日首发（[Wikipedia MATE][wp-mate]）。它直接续 GNOME 2 的代码与应用，因包名冲突换了一套西语名：Nautilus 变 Caja、Evince 变 Atril、gedit 变 Pluma、Metacity 变 Marco、File Roller 变 Engrampa。技术债逐年还清：1.6（2013）从 GConf 换 GSettings、从 CORBA 换 D-Bus，1.20（2018）上 GTK 3，1.28（2024 年 2 月）仍在发版。一个拒绝新范式的分家活到了今天，这本身就是数据点。

Cinnamon 走的是先合作后分家。Linux Mint 2011 年 11 月的 Mint 12 同船发行 MATE 与带 MGSE（Mint GNOME Shell Extensions）的 GNOME 3，用扩展把任务栏等加回 Shell。Mint 的结题判断记在 [Wikipedia Cinnamon 条目][wp-cinnamon]：有些缺失功能无法用扩展补回，上游也不愿合作。2011 年 12 月首个版本，2012 年 1 月正式定名 Project Cinnamon fork GNOME Shell。窗口管理器 Muffin 同月从 Mutter 分出，文件管理器 Nemo 2012 年 9 月从 Nautilus 分出。2.0.0（2013 年 10 月 9 日）宣布完全独立，安装不再依赖 GNOME。

Unity 是第三条支线。Canonical 2010 年 10 月停止 GNOME 壳层合作，转而自研 Unity，2017 年 Ubuntu 17.10 回到定制 GNOME Shell（[Wikipedia GNOME 3][wp-gnome3]）。[Wikipedia GNOME 条目][wp-gnome]还把 LXQt、Budgie、Pantheon、COSMIC 的出现部分归因于 GNOME 3 的接受度。一次范式切换的社区成本，活样本全在这里。

### 6.2 节奏与长迁移的收法

半年一发（3 月与 9 月），版本号单数字递增。GNOME OS 作为滚动测试场存在，扩展迁移这类破坏性变更官方建议先在 GNOME OS 上验证（[GNOME 45 博客][e45]）。

X11 到 Wayland 的迁移收口是个范本。最后一步（50 删 X11 会话）提前九个月出 FAQ，说明影响面（XWayland 保留，应用不受影响），给下游发行版留出两个发布周期的缓冲（[X11 Session Removal FAQ][x11faq]）。十五年量级的迁移，最后一步走得反而最有章法。

## 对 vesper 的启示

1. 概念参考 GNOME 的 GTK 与 libadwaita 分层。visor 站 GTK 位：通用控件库，不掺桌面语义。vesper 站 libadwaita 加 Shell 位：Mono1 单色加圆角这套视觉语言与壳层语义长在自己仓里。将来 visor 若要加壳层形状（比如任务栏条），按通用容器提供，桌面概念映射留在 vesper。
2. 概念参考 GNOME 的 gnome-settings-daemon 插件分工。单进程期内把「设置执行」立成进程内模块边界，媒体键、电源策略这类没有窗口的事都挂这条线，D6 拆进程时按它切，桌面结构不推倒。
3. 概念参考 GNOME 的 core app 名册治理。App 口每新增一个 App 都按 incubator 思路走：先进孵化判例，过了「三梯队后端都成立」的检查，再进出厂名册。终端先行的路线 GNOME 走过（Terminal 换位 Console），它的教训是下游接受度先于功能完备。
4. 概念参考 GNOME 的 HIG Patterns 分类。vesper 的设计文档按容器、导航、反馈、控件四类组织 pattern，不按 visor 现有八个控件列清单。单色视觉语言的身份该在这样的文档里成文，pattern 比控件活得久。
5. 概念参考 GNOME Shell 扩展的版本闸。vesper 单进程编译期没有动态扩展问题，但 D6 之后的像素与事件跨进程协议就是自己的内部 ABI。协议头留版本字段，不匹配拒绝加载。这条教训 GNOME 用十年生态折损换来。
6. 概念参考 GNOME 3 的 fallback mode 教训。残缺的退路比没有退路更伤信任。vesper 三梯队（X11、fbdev、Cinux）每个后端要么全功能，要么在文档里明写差距，不提供看似能跑其实缺件的模式。
7. 概念参考 GNOME 的 X11 到 Wayland 长迁移收法。后端替换按里程碑逐步收，最后一步提前公告、留兼容层、给 FAQ。D 系列每刀带验收就是这个策略的最小版，将来 fbdev 或 Cinux 收主线时照此办理。
8. 概念参考 GNOME 的扩展生态两面性。扩展机制接住了流失用户，也让 Shell 背上无兼容承诺的债。vesper 教学向不需要动态扩展，转译过来就是两条：App 口与宿主表的抽象不为单个 App 弯曲，也不堵死将来给 App 放脚本能力的路。

## 参考来源

- [gjs-arch] https://gjs.guide/extensions/overview/architecture.html — 支撑 1、2.3、3.1（Shell 与 St/Clutter 栈、能力清单、无沙箱引语、可复用模块）
- [gjs-anatomy] https://gjs.guide/extensions/overview/anatomy.html — 支撑 3.1（zip 结构、生命周期、session-modes、版本闸、GNOME 45 ES Modules）
- [gjs-review] https://gjs.guide/extensions/review-guidelines/review-guidelines.html — 支撑 3.1（审核红线）
- [e45] https://blogs.gnome.org/shell-dev/2023/09/02/extensions-in-gnome-45 — 支撑 3.1、6.2（ES Modules 迁移与 GNOME OS 验证建议）
- [ext-ai] https://linuxiac.com/gnome-will-reject-shell-extensions-with-ai-generated-code — 支撑 3.1、3.3（2025-12 扩展审核拒 AI 低质代码）
- [disc-break] https://discourse.gnome.org/t/gnome-shell-breaks-catastrophically-every-few-months/8124 — 支撑 3.3（扩展周期性损坏的常态样本）
- [appsgnome] https://apps.gnome.org — 支撑 4.1（Core/Circle/Developer Tools 定义与 2026-10 名册）
- [mcatanzaro] https://blogs.gnome.org/mcatanzaro/2023/05/10/gnome-core-apps-update — 支撑 2.3、4.2（名册流水账、Console 教训、Incubator、Photos 维护危机、Screenshot 内置）
- [circle] https://circle.gnome.org — 支撑 4.3（Circle 申请审核与收益）
- [phoronix-stats] https://www.phoronix.com/news/GNOME-2025-Code-Stats — 支撑 4.3（Core 44% C、Circle 41.7% Rust）
- [circle-ai] https://linuxiac.com/gnome-circle-adopts-ai-policy-and-stops-new-app-submissions — 支撑 4.3（2026-05 Circle 暂停收新）
- [circle-ai-px] https://www.phoronix.com/news/GNOME-Circle-No-AI-Slop — 支撑 4.3（同上，官方口径补充）
- [hig] https://developer.gnome.org/hig/ — 支撑 5（HIG 结构与 Platform 定义）
- [aday-hig] https://blogs.gnome.org/aday/2021/05/20/new-human-interface-guidelines — 支撑 5（HIG 重写预告）
- [register-hig] https://www.theregister.com/software/2021/08/09/new_gnome_human_interface_guidelines_now_official-and-obviously-some-people-hate-it/1384677 — 支撑 5（2021-08 新 HIG 转正与社区反应）
- [tesk] https://tesk.page/2024/06/03/libadwaita-splitting-gtk-and-design-language — 支撑 2.1（GTK 与设计语言切割）
- [wp-adwaita] https://en.wikipedia.org/wiki/Adwaita_(design_language) — 支撑 2.2（Adwaita 定位）
- [adw-theming] https://discourse.gnome.org/t/why-and-how-libadwaita-prevents-theming/17079 — 支撑 2.2（主题机制官方解释）
- [osnews] https://www.osnews.com/story/133955/gnome-to-prevent-theming-wider-community-not-happy — 支撑 2.2（2021 社区反弹）
- [gsd-plugins] https://github.com/GNOME/gnome-settings-daemon/tree/master/plugins — 支撑 2.4（插件清单，GNOME GitLab 的 GitHub 镜像）
- [lgsb] https://github.com/turbofish-pk/linux-graphics-stack-book/blob/main/chapters/part-07c-desktop-frameworks/ch39d-gnome.md — 支撑 1、2.3（组件图与「几乎全 JS」，已打开全文核对；原 jreuben11 地址已失效，该仓库为同书现存位置）
- [wp-shell] https://en.wikipedia.org/wiki/GNOME_Shell — 支撑 1、2.3（C 加 JS 的 Mutter 插件定位）
- [wp-gnome] https://en.wikipedia.org/wiki/GNOME — 支撑 1、2.1、3.2、6、6.1（起源、版本全表、Classic 即扩展、争议溢出）
- [wp-gnome3] https://en.wikipedia.org/wiki/GNOME_3 — 支撑 3.2、6.1（Torvalds 评价与回流、Canonical 分合）
- [wp-mate] https://en.wikipedia.org/wiki/MATE_(software) — 支撑 6.1（Perberos、分家时间线、改名表、技术迁移）
- [wp-cinnamon] https://en.wikipedia.org/wiki/Cinnamon_(software) — 支撑 6.1（MGSE 到分家、Muffin、Nemo、2.0 独立）
- [dedoimedo] https://www.dedoimedo.com/computers/gnome-3-fallback.html — 支撑 6.1（fallback mode 当年实测）
- [x11faq] https://blogs.gnome.org/alatiera/2025/06/23/x11-session-removal-faq — 支撑 1、2.4、6.2（X11 会话移除预告与 XWayland 保留）
