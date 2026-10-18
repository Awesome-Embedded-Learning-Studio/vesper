# 桌面出厂 App 套件对比：KDE Gear、GNOME Core Apps 与 Xfce Goodies

> 调研日期 2026-10-17 · 服务对象：vesper 手搓教学桌面 · 来源：网络检索加本地材料

## 0. 口径：「出厂」在三家的意思不一样

对比前先统一词汇，否则表格对不齐。

KDE Gear 是发布捆，不是默认安装清单。Wikipedia 记载它是「a set of applications and supporting libraries that are developed by the KDE community」，超过两百个应用按同一节奏发版，名字从 2021 年 4 月那次发版开始启用，前身 KDE Applications Bundle 的首次捆发是 14.12（2014-12-17）。捆内的件叫 Gear，捆外自谋节奏的叫 Extragear。发行版装哪几个由发行版定，KDE 只捆不选。

GNOME Core Apps 是治理出来的名册。apps.gnome.org 把应用分三级：Core（桌面常规任务、通常预装）、Circle（第三方精品荣誉圈）、Developer Tools。按 2026 年 10 月清点 Core 共 28 个（本仓 gnome-architecture.md §4.1 的清点，口径沿用它）。名册有进出机制，进出的历史本身就是「什么算出厂 App」的判例库。

Xfce 是三层结构：核心桌面组件（xfwm4、xfce4-panel、xfce4-session、xfce4-settings、xfdesktop）、apps（Thunar、xfce4-terminal、Mousepad 这些独立发布的应用）、Goodies（额外应用与插件的集散地，官方首页自述「Additional software packages and plugins to existing software, like the panel or the file manager, are provided by the Xfce Goodies Project」）。Goodies 站点正在退役，文档向 docs.xfce.org 迁移（Xfce wiki 的 goodies-decomm 页，经检索摘要转引）。

「哪些 App 缺了桌面就残」最直接的活证据，是 KDE 社区 2024 年 4 月的 Core Apps Group 提案（discuss.kde.org，radbirb 发起）：把一批 App 从 Gear 拆出来，跟 Plasma 同节奏发版。提案名单约 25 件，跟帖吵的全是「谁不够格」，音乐视频播放器、相机、扫描、分区工具被点名建议排除，文件管理器、终端、设置这类没人争。社区自己替我们做了一遍最小集筛选。

依赖深度，本文分三档（本文归纳，用于后文表格）。一档平台库绑定：KIO、KParts、libadwaita、VTE、GStreamer，换工具箱等于重写。二档格式与数据底座：gdk-pixbuf、glycin、Poppler、libarchive、GtkSourceView、libgtop，可替换的解码或取数库。三档进程外借：本体只是壳，实际干活的是外部程序。vesper 是 Mono1 加零第三方依赖，三档在它身上的对应物很整齐：一档就是 visor 控件（自家），二档必须自写，三档落在 spawn 外部程序上（正好是 D6 主线）。

## 1. KDE Gear：两百件的捆，出厂名单交给发行版

代表 App 的类别与依赖深度（依赖只列有出处的那几件，其余标未核实）：

| App | 类别 | 依赖深度 | 出处 |
|---|---|---|---|
| Dolphin | 文件管理器 | 一档。KIO 干网络访问、回收站、缩略图，Arch Wiki 记载原话是它用 KIO slaves 而 GTK 系文件管理器用 GVFS | Arch Wiki（转引） |
| Konsole | 终端 | 一档。KParts 部件，Gear 23.04 公告说它能嵌进 Kate、Dolphin、KDevelop | kde.org |
| Kate 与 KWrite | 文本编辑 | 一档。KTextEditor 控件是 Kate 的本体，KWrite 是同控件的薄壳 | 本仓 kde-plasma-architecture.md §2.1 |
| Ark | 归档器 | 二档加三档混合。libarchive 与 karchive 管 tar 系，同时也是数个命令行归档器的前端 | Wikipedia |
| Gwenview | 图片查看 | 一档。KDE Frameworks 加 Qt，Gear 24.02 升到 Qt 6 与 KF6 | Wikipedia |
| Okular | 文档查看 | 一档加二档。基于 KParts 可嵌入，每种格式一个后端库（PDF 是 Poppler、PostScript 是 libspectre、TIFF、CHM、DjVu 各有后端） | Wikipedia |
| Elisa | 音乐播放 | 未核实解码栈，此处只登名册 | Wikipedia |
| Spectacle | 截图 | 提案名单在列，依赖深度未逐件核实 | discuss.kde.org |
| plasma-systemmonitor | 系统监视器 | 不在 Gear。随 Plasma 发版，表底座 libksysguard 与前代 KSysGuard 共用 | KDE Discuss |

两个归属特记。其一，系统监视器的换代：KSysGuard 在 Plasma 5.21（2021 年初，9to5Linux 与 Manjaro 论坛的检索摘要转引）里被 plasma-systemmonitor 顶替，KDE Discuss 跟帖原话是 ksysguard「uses the same libksysguard required by plasma-systemmonitor」，即 UI 换代、数据层不动。其二，System Settings、Discover、KRunner 不走 Gear，kde.org 的 Plasma 页把它们当桌面本体的内置能力介绍。所以「KDE 出厂 App」实际是两拨：随 Plasma workspace 走的桌面件，加发行版从 Gear 里自选的一筐。

Core Apps Group 提案值得多看两眼，因为它把「出厂」的判据摆上了台面。提案的动机是发布节奏错位：Gear 按「自己的三个月节奏」发（提案原话），同一版 Plasma 配上五花八门的 App 版本。提案的解法是照 GNOME 的 Core 模型立一个 KDE Core 组「strictly released alongside Plasma releases」。跟帖里 ngraham（KDE 开发者）倾向更简单的方案：Gear 与 Plasma 直接同步节奏。争议集中在边界件，反方论点是 Dolphin、Konsole 本身与桌面环境无关，挪进 Plasma 会「send the implicit message that the app isn't DE agnostic」。对 vesper 的价值：App 与桌面本体的发布（构建）耦合度是一个真实的设计变量，不是想当然的「App 跟桌面一起发」。

## 2. GNOME Core Apps：名册治理与两次「吸收」

28 件名册按用途分组（apps.gnome.org，2026-10，经本仓 gnome-architecture.md §4.1 清点转引）：

| 分组 | 应用 |
|---|---|
| 文件与文档 | Files（Nautilus）、Text Editor、Document Viewer（Papers）、Document Scanner、Logs |
| 系统管理 | Settings、Disks、Disk Usage Analyzer（Baobab）、System Monitor、Software |
| 终端与帮助 | Console、Help（Yelp）、Tour |
| 影音 | Image Viewer（Loupe）、Video Player（Showtime）、Audio Player（Decibels）、Music、Camera（Snapshot） |
| 网络与信息 | Web（Epiphany）、Connections、Maps、Weather |
| 个人事务 | Calendar、Contacts、Clocks、Calculator |
| 字符与字体 | Fonts、Characters |

名册进出的历史里有两次吸收判例（mcatanzaro 2023 年复盘，经 gnome-architecture.md §4.2 转引）。GNOME 41（2021）移除 file-roller，理由写在括号里：Files 自己能开压缩包。GNOME 42（2022）移除 Screenshot，因为 Shell 内置了截图。也就是说 GNOME 用「桌面件吃掉 App」的办法减名册，归档器并进文件管理器，截图并进桌面壳。同一条时间线上还有两次新写上位：Text Editor 换掉 gedit（41），Console 换掉 gnome-terminal（42）。

关键件的依赖深度：

| App | 类别 | 依赖深度 | 出处 |
|---|---|---|---|
| Files | 文件管理器 | 一档加二档。BLFS 49.3 的必需依赖表：gexiv2、gnome-autoar、gnome-desktop、libadwaita、libportal、libseccomp、tinysparql（tracker 的后继），localsearch 标注「required at runtime」 | BLFS |
| Console | 终端 | 一档。vte-gtk4（最低 0.77）、libadwaita（最低 1.8）、GTK（最低 4.19）、GLib、pango | Wikipedia |
| Text Editor | 文本编辑 | 二档。GtkSourceView 语法引擎，与编辑器同作者维护 | GNOME Blogs（转引） |
| System Monitor | 系统监视器 | 一档加二档。BLFS 49.1 必需依赖：libgtop、Gtkmm、libadwaita、adwaita-icon-theme、librsvg | BLFS |
| Loupe | 图片查看 | 二档。Rust 写，图像解码靠 glycin，格式支持随 glycin 走，GNOME 45（2023-09）接替 Eye of GNOME | Wikipedia |
| Showtime 与 Decibels | 影音 | 未逐件核实。GNOME 影音件普遍坐在 GStreamer 上，此句为通识判断，未附单一出处 | 未查到单件出处 |

Files 的依赖表给了一个教学样本：文件管理器牵动的库面在三类 App 里几乎最宽（解压、元数据、沙箱、桌面规格、索引检索），而且 tinysparql 与 localsearch 这对检索栈是「不装就转不稳」的硬运行时依赖（BLFS 把 localsearch 标为运行时必需，Fedora 讨论里有移除后周期性崩溃的反馈，经检索摘要转引）。GNOME 把最重的件做成了名册第一号。

## 3. Xfce：核心组件之外的 apps 与 Goodies

Xfce 的 App 名册（docs.xfce.org 的 apps 页所列，加 Thunar 与归档器）：

| App | 类别 | 依赖深度 | 出处 |
|---|---|---|---|
| Thunar | 文件管理器 | 二档为主。必需：GTK、GLib、exo、libxfce4util、libxfce4ui、xfconf。gvfs 是可选件（回收站、挂载、远端都靠它），thunarx 是它的插件接口 | docs.xfce.org |
| xfce4-terminal | 终端 | 一档。基于 Vte 控件，与 gnome-terminal 同底座，官方文档自述 Vte「probably not the fastest terminal emulation library」 | docs.xfce.org |
| Mousepad | 文本编辑 | 二档。必需仅 GLib、GTK、GtkSourceView。定位原话「an editor for quickly editing text files, not a development environment」 | docs.xfce.org |
| Ristretto | 图片查看 | 二档。gdk-pixbuf 解码，更多格式靠可选 loader（libheif、libjxl、librsvg 等） | docs.xfce.org |
| Parole | 媒体播放 | 一档。必需 GStreamer 与 GTK，「based on the GStreamer framework」，1.2 一线 2025-05 还有 parole 4.20.0 | docs.xfce.org |
| xfce4-taskmanager | 系统监视器 | 几乎零档。必需 GTK3、Cairo、Xmu，连 libwnck 都是可选（装了才显示进程图标） | docs.xfce.org |
| Xarchiver | 归档器 | 三档。纯外部工具前端，Wikipedia 记它是「a front-end to various command line archiving tools」，格式取决于系统里装没装 zip、tar 这些工具，没装就造不出对应格式的包 | Wikipedia |
| xfce4-screenshooter、Catfish、Gigolo、Orage、xfburn、xfce4-dict | 外围 | 名册在列，深度未逐件核实 | docs.xfce.org |

Xfce 的借件路线（lightweight-and-classic-desktops.md §6 的归纳：桌面本体以下的层全部借现成的）延续到了 App 层：终端仿真借 VTE，编辑器引擎借 GtkSourceView，图像解码借 gdk-pixbuf，媒体借 GStreamer，自己写的只有壳与工作流。更极端的是归档器：Xarchiver 根本不在 Xfce 仓里，它是独立项目（作者 Ingo Brückl），维基记载它被当作 Xfce 与 LXDE 的默认归档器。「出厂 App」可以连仓都不进，这是发行版拼装桌面的极端形态。

Goodies 这一层是插件生态：面板插件（Debian 的 xfce4-goodies 元包收电池、剪贴板这些插件）加 Thunar 的 thunarx 插件加外围应用。它对 vesper 的参考价值不大（vesper 无插件加载机制，单进程编译期注册），但它的退役本身是一条注脚：一个靠 wiki 站点维持的附加件目录，活不过文档平台化的迭代。

## 4. 横向对照与「最小可用桌面 App 集」

把三家放在一张表上（空格表示该家没有此类的固定出厂件）：

| 类别 | KDE | GNOME Core | Xfce | vesper 判定 |
|---|---|---|---|---|
| 文件管理器 | Dolphin | Files | Thunar | 留，第二波 |
| 终端 | Konsole | Console | xfce4-terminal | 留，第一波（D3、D4） |
| 文本编辑器 | Kate、KWrite | Text Editor | Mousepad | 留，第四波 |
| 系统监视器 | plasma-systemmonitor（随 Plasma） | System Monitor | xfce4-taskmanager | 留，穿插的便宜件 |
| 设置 GUI | System Settings（随 Plasma） | Settings | xfce4-settings | 缓，配置库先行 |
| 归档器 | Ark | file-roller 已退位 | Xarchiver（仓外件） | 缓，优先并进文件管理器 |
| 图片查看 | Gwenview | Loupe | Ristretto | 缓到 RGB565 站之后 |
| 媒体播放 | Elisa、Dragon Player | Showtime、Decibels、Music | Parole | 推迟，解码栈不存在 |
| 截图 | Spectacle | Screenshot 已退位（Shell 内置） | xfce4-screenshooter | 并进桌面本体 |
| 计算器 | KCalc | Calculator | | 锦上添花 |
| 游戏 | kdegames 一大批 | | | 锦上添花，音频判例的触发件 |

逐类定去留的依据分三层说。

缺了就不像桌面的是终端与文件管理器。三家的出厂件都有这两类，KDE 的 Core Apps Group 提案里这两件零争议，跟帖那句「Plasma is useless without System Settings and no cross-distro app will replace it」给出的判据是「没有它桌面就残且外人补不上」。vesper 对应的判据更朴素：图标能拉起终端之前，桌面只是一块画布。能看能管文件之前，它只是一个终端全屏器。程序启动器（桌面图标）加终端加文件管理器，是「像桌面」的最小三件。

第二梯队是文本编辑器与系统监视器。三家也件件都有，但 KDE 提案跟帖对 System Monitor 是否 core 有争议，文本编辑器被建议「KWrite or Kate」二选一。它们是「有了像桌面，没有像半成品」。截图与归档器被 GNOME 证明可以不独立存在：截图是桌面本体的免费副产品（帧缓冲本来就在手里），归档可以并进文件管理器（gnome-autoar 判例）或者做成本地工具的薄壳（Xarchiver 判例）。

彩色之前不做的是图片查看与媒体播放。两类的本体都是解码栈（gdk-pixbuf、glycin、GStreamer），在 1bpp 黑白下看图价值趋近于零，vesper 的音频输出通道一条都没有。desktop-infrastructure.md §3 已把音频栈整体列为将来站，小游戏是那站的触发判例，媒体播放顺位排在小游戏之后。

## 5. 候选 App 的依赖面与缺件编号映射

对照 vesper-gap-analysis.md 的编号（编号后的短语是缺件的简写），每个候选 App 列它要吃的件：

| App | 吃的缺口 | 额外依赖面 | 刀级感受 |
|---|---|---|---|
| 终端 | 2.1 终端网格（D3、D4 已排期）加 3.1 spawn 与 PTY 加 3.2 信号（SIGWINCH、SIGCHLD）加 3.3 poll 编排加 1.2 剪贴板（D4 接线已裁定：拖选写 PRIMARY、中键粘贴）加 1.6 resize（联动 TIOCSWINSZ）加 1.7 滚轮与 2.2 视口（回滚） | 无新件，全是已编队缺口 | 大刀已排期，App 化本身是小刀 |
| 文件管理器 | 2.4 列表视图加 3.5 文件封装（目录枚举）加 1.9 持久化（末次目录与窗口位置）加 1.1 弹层（右键菜单）加 1.7 与 2.2（长列表滚动） | mime 判断用 desktop-infrastructure.md §4.4 拍板的自养小 glob 表，trash 判例同文启示 4（删除即移动加记事） | 2.4 中刀加 3.5 中刀，主体两刀 |
| 系统监视器 | 3.4 定时器（刷新节拍）加 3.5 文件封装（读 /proc）加 2.4 列表视图（进程表是列表第二消费者） | 读 /proc 是 Linux 侧系统调用的薄封装，对应各家 libgtop 的位置，Cinux 侧将来换同名文件 | 小刀，四件里最便宜 |
| 文本编辑器 | 2.3 多行编辑（大刀，回填 visor）加 2.2 视口加 1.7 滚轮加 3.5 文件封装加 2.6 输入法入口（中文编辑，推迟） | 无 | 2.3 本体是大刀 |
| 图片查看 | RGB565 与解码器（无编号，渲染接口冻结前不做） | 位图解码自写，MonoBitmap 现成只够画图标 | 推迟整站 |
| 归档器 | 3.1 spawn（Xarchiver 模型：外部 tar 与 zip 的薄壳）或自写最小 zip 读取器，挂 3.5 | spawn 已是 D4 与 D6 的主线件 | 小刀，两模型二选一 |
| 小游戏 | 音频输出（无编号，desktop-infrastructure.md §3 的将来站） | 事件五件套与 MonoBitmap 现成，棋盘类（扫雷、贪吃蛇）KDE 的 kdegames 是批量先例 | 触发音频站时顺刀 |

两点补充。其一，四件第一波 App 全部不需要新宿主能力，终端吃 PTY（D4 已定），文件管理器吃文件系统，监视器吃 /proc 与定时器，编辑器吃纯控件，宿主表三口一个不动。这正是「App 建设不阻塞 D 系列主线」的结构保证。其二，系统监视器的 /proc 是教学论据的现成教具：GUI 离操作系统很远，监视器这个最像「系统」的 App，数据源根本不在 GUI 层，在内核的伪文件系统里。

## 6. 四件套对 App 口的直接映射

交接文档 §1 拍板的 App 口是四个动作：报名字、给图标、造窗口内容、收生命周期回调。四件逐一对上：

| App | 报名字 | 给图标 | 造窗口内容 | 收生命周期 |
|---|---|---|---|---|
| Terminal | "Terminal" | 终端字形 MonoBitmap | 终端控件（D3 产出）加 resize 联动 TIOCSWINSZ | 关窗收 PTY 子进程（信号加 reap），四件里第一个真实消费者 |
| Files | "Files" | 文件夹字形 | 列表视图（2.4）加路径行 | 末次目录写进配置（1.9） |
| System Monitor | "System Monitor" | 仪表字形 | 定时器驱动的进程表与负载读数 | 注销定时器（3.4 的取消语义由它逼出） |
| Text Editor | "Text Editor" | 纸笔字形 | 多行编辑控件（2.3） | 有未存改动时拦一道关窗（dirty 位，第一块关窗前拦截判例） |

四件各自逼出一条 App 口语义，这是「每新增一个 App 都是对这条边界的判例」（交接文档 §1 原话）的具体展开：终端逼出生命周期与子进程归属，监视器逼出定时器注销，编辑器逼出关窗拦截，文件管理器逼出状态持久化。App 口不是一上来设计全，是让四件各交一块作业。

单进程期这四件都是编译进来的注册模块，D6 后同一接口换真进程（交接文档 §1）。GNOME 的 Console 新写上位判例提示了接口与实现的分界：名册上是「Console」，底下从 gnome-terminal 换成了 kgx，用户视角无感。vesper 的 App 注册表（缺口 1.10）按 desktop entry 字段语义设计（desktop-infrastructure.md §4.2 已拍），将来换进程实现时注册表本身不动。

## 7. 建设顺序建议

结合 construction-order.md 的先例律（终端第一批的律五、设置 GUI 晚到的律六、生态件后置的律八）与缺件清单的刀级：

1. 终端第一，D4 收刀即 App 化。控件先行、应用后包，这是 VTE 的分界判例（terminal-emulator-internals.md §1：控件不是应用，填进窗口就能用）。D3 产出控件，D4 接真 bash，App 化只是把控件装进 App 口的壳，小刀。
2. 系统监视器可以抢跑占位。App 注册表（1.10）排在 D2 前，那时终端控件还没有，注册表需要第一个报名的 App 来当判例。监视器只吃 3.4 定时器加读 /proc，是四件里依赖面最小的，拿只读进程表当 App 口第一块试金石，桌面也从「有壳」变「有 App」。
3. 文件管理器第三，按既定第二位顺延一位。它是重写大户（律四：kfm 到 Konqueror、gmc 到 Nautilus、xftree 到 Thunar 全是换代史），第一版把边界定小：列表视图加枚举加 trash，不做缩略图不做远端不做双栏。桌面面接管的候选身份保留（pcmanfm-qt 与 Explorer 的判例，lightweight-and-classic-desktops.md §7），但第一版不接管。
4. 文本编辑器第四，等 2.3 回填 visor。Mousepad 的定位句就是它的规格书：快速改文本文件的编辑器，不是开发环境。语法高亮、会话、插件全部不做。
5. 设置 GUI 晚到，配置库先行（修正二与律六）。Xfce 的 settings manager 距立项十二年，单色桌面的设置面天然小（字号、桌面图案、键位），ini 条目攒够再立 GUI。
6. 锦上添花波：截图随桌面本体（一帧拷贝加大刀后的文件写入），图片查看等 RGB565，归档器贴在文件管理器稳定之后（外部工具薄壳优先，教学上顺带练 spawn），小游戏等音频站（它是触发件不是等待件）。

App 波次与 D 系列的关系是一条平行线：D5 到 D6 的宿主与进程刀照走，App 增员不动桌面本体。KDE Core Apps Group 提案抱怨的正是 Gear 与 Plasma 节奏互相拖累，vesper 单进程编译期注册天然没有这个问题，D6 拆进程时才有第一个真实节奏问题，到时候「App 与桌面本体分开发版」的业界形态可以直接当参照。

## 对 vesper 的启示

- 概念参考 GNOME Core 的名册治理。名册进出有两条成文标准：下游接受度先于功能完备（Console 事件），维护断档比功能缺失更致命（Photos 出局）。vesper 的 App 注册表（1.10）给「是否显示」位配一个成熟度语义，就是这两条标准的极简版。
- 概念参考 KDE 社区的 Core Apps Group 提案。core 的判据是「桌面没它就残且外人补不上」，争议件（影音、相机、扫描）用「有了更好」划出去。vesper 的 App 波次排序可以直接抄这个判据：终端与文件管理器是 core，其余按依赖面就绪排。
- 概念参考 GNOME Files 的 gnome-autoar 吸收。归档器类别被文件管理器吃掉，截图类别被桌面壳吃掉，类别清单不是一成不变的，能并的并，并掉一件少养一件。
- 概念参考 Xfce 的 Mousepad 定位。「快速改文本文件，不是开发环境」，这一句就是 vesper 文本编辑器第一版的全部需求文档。
- 概念参考 Xarchiver 的外部工具前端。零解码依赖的归档路：本体是壳，tar 与 zip 是外部程序。vesper 零第三方依赖的纪律下，这是归档器唯一不用自写解码器的形状，还顺带是 spawn 的教学判例。
- 概念参考 libgtop 的表底座。系统监视器与桌面件之间隔一层取数库，GNOME 侧叫 libgtop，KDE 侧叫 libksysguard（两代 UI 共用一个底座是它的证据）。vesper 的同构物是一张读 /proc 的函数表，UI 换代、表不动。

## 参考来源

网络出处（未标转引的均为本次直接打开核实）：

- https://en.wikipedia.org/wiki/KDE_Gear 支撑 §0 与 §1（定义、两百件、2021-04 改名、14.12 首发、Extragear、分类名册）。
- https://kde.org/announcements/gear/23.04.0 支撑 §1（Konsole 的 KParts 嵌入与 Windows 移植公告）。
- https://discuss.kde.org/t/a-kde-gear-proposal-splitting-core-apps-from-gear-kde-core-app-group/13794 支撑 §0、§1、§4、§7（Core Apps Group 提案名单、节奏动机、跟帖争议、ngraham 的同步方案）。
- https://wiki.archlinux.org/title/Dolphin （经检索摘要转引，未直接开页）支撑 §1（Dolphin 用 KIO slaves、GTK 系用 GVFS 的对照）。
- https://apps.gnome.org/ 支撑 §2（三级分类与名册，2026-10 清点经 gnome-architecture.md 转引）。
- https://www.linuxfromscratch.org/blfs/view/13.0-systemd/gnome/nautilus.html 支撑 §2（Nautilus 49.3 必需依赖表、localsearch 运行时必需）。
- https://en.wikipedia.org/wiki/GNOME_Console 支撑 §2（vte-gtk4、libadwaita、GTK 版本下限）。
- https://blogs.gnome.org/chergert/category/gtksourceview （经检索摘要转引）支撑 §2（Text Editor 与 GtkSourceView 同作者）。
- https://www.linuxfromscratch.org/blfs/view/13.0-systemd/gnome/gnome-system-monitor.html 支撑 §2（libgtop、Gtkmm、libadwaita 必需依赖）。
- https://en.wikipedia.org/wiki/Loupe_(software) 支撑 §2（Rust 加 glycin、GNOME 45 接替 Eye of GNOME）。
- https://discuss.kde.org/t/bring-ksysguard-back-as-neither-plasma-systemmonitor-ksystemstats-are-any-good/8469 支撑 §1（libksysguard 两代共用原话）。
- https://kde.org/plasma-desktop/ 支撑 §1（Discover、System Settings、KRunner 随 Plasma 的介绍口径）。
- https://docs.xfce.org/apps/start 支撑 §3（apps 名册：Catfish、Ristretto、Parole、xfce4-taskmanager、xfce4-terminal、Mousepad 等）。
- https://docs.xfce.org/xfce/thunar/start 支撑 §3（必需与可选依赖、thunarx、gvfs 的角色、设计目标句）。
- https://docs.xfce.org/apps/xfce4-terminal/start 支撑 §3（基于 Vte、probably not the fastest 原话）。
- https://docs.xfce.org/apps/mousepad/start 支撑 §3（GtkSourceView 必需、定位原话）。
- https://docs.xfce.org/apps/ristretto/start 支撑 §3（gdk-pixbuf 与可选 loader 清单）。
- https://docs.xfce.org/apps/parole/start 支撑 §3（GStreamer 必需、依赖表、4.20.0）。
- https://docs.xfce.org/apps/xfce4-taskmanager/start 支撑 §3（必需依赖表、libwnck 可选）。
- https://en.wikipedia.org/wiki/Xarchiver 支撑 §3（外部命令行工具前端的定位、Xfce 与 LXDE 默认归档器记载、Ingo Brückl）。
- http://goodies.xfce.org 支撑 §0（Goodies 项目定义句，经检索摘要转引）。
- https://wiki.xfce.org/projects/goodies-decomm/start （经检索摘要转引）支撑 §0 与 §3（Goodies 站退役、文档迁移）。
- https://packages.debian.org/trixie/xfce4-goodies （经检索摘要转引）支撑 §3（元包内容：电池、剪贴板插件、artwork）。
- https://en.wikipedia.org/wiki/Ark_(software) 支撑 §1（libarchive 与 karchive、命令行归档器前端、1997 年 kzip 起家）。
- https://en.wikipedia.org/wiki/Gwenview 支撑 §1（KF 与 Qt 平台、24.02 升 Qt6）。
- https://en.wikipedia.org/wiki/Okular 支撑 §1（KParts 嵌入、逐格式后端清单）。

本地材料：

- /home/charliechen/vesper/pack-26-desktop-handover.md 支撑 §5 至 §7（App 口四动作、D 系列刀序、D6 进程边界拍板）。
- documents/research/vesper-gap-analysis.md 支撑 §5 全部缺口编号与刀级、§7（修正一与修正二）。
- documents/research/gnome-architecture.md 支撑 §2（Core 名册与分组、mcatanzaro 复盘的 file-roller 与 Screenshot 退位、Console 新写上位）。
- documents/research/kde-plasma-architecture.md 支撑 §1（KTextEditor 归 Frameworks）。
- documents/research/lightweight-and-classic-desktops.md 支撑 §3（借件路线）、§4（pcmanfm-qt 与 Explorer 的桌面面兼任判例）、§7（八件概念件）。
- documents/research/desktop-infrastructure.md 支撑 §4（音频栈将来站）、§5（mime 半取拍板、trash 判例）、§6（desktop entry 字段语义）。
- documents/research/terminal-emulator-internals.md 支撑 §5（TIOCSWINSZ 链路）、§7（VTE 控件与应用的分界）。
- documents/research/construction-order.md 支撑 §7（律四、律五、律六、律八与修正二）。
