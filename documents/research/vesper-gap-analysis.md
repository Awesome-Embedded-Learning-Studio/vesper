# vesper 缺件清单：桌面本体、App 层与 GUI 无关基建的逐项核对

> 调研日期 2026-10-17（同日修订：两篇草稿到稿后缝合增补十一条） · 服务对象：vesper 手搓教学桌面 · 来源：网络检索加本地材料

## 0. 口径与核对基准

这份清单是给作者拍板的决策底稿。每条缺口写四样：缺口是什么、成熟桌面的对应物（带 reference 标签）、visor 与 vesper 现状（有、无、半个，注明出处文件路径）、处置倾向三选一（回填 visor、vesper 自建、推迟）加一句理由。文末按条目标出小刀、中刀或大刀。

核对基准。visor 侧全部结论来自对 `/home/charliechen/visor` 工作树的逐文件查看（HEAD `9294df8`，2026-10-16，与交接文档记载一致），其中「没有」的判断都做过全量检索（滚轮、定时器、剪贴板、菜单、spawn、文件与环境变量各查过一遍），不凭印象。vesper 侧现状是 `~/vesper` 仓只有 documents 与交接文档，代码一刀未开，所以 vesper 的「现状」一律引 `pack-26-desktop-handover.md` 的拍板与刀序。

两篇草稿已到稿。`window-and-session-theory.md` 与 `app-suite-survey.md` 在初版核对时不在 `documents/research/` 目录，本文按其余六篇草稿加交接文档先行成稿；2026-10-17 同日两稿落盘，本文随即补了一轮：窗口操作语义、焦点模型缝进第 1 节（新增 1.13 至 1.17），会话恢复的形状就地更新进 1.12，App 依赖映射里清单未覆盖的条目缝进第 2 节（新增 2.7 至 2.10），3.4 定时器就地补注销语义，完整性审查点名补进 1.18 与 1.19，工作量估计与第 5 节账目同步改写。旧条目编号一律不动，外部草稿对本文的既有引用不受影响。

一个对齐全景的参照。轻量与经典桌面调研把「能叫桌面」的东西归成八件概念件，对照 vesper 的 D 系列与本文条目：

| 概念件 | vesper 归宿 |
|---|---|
| 窗口管理 | D1 拖动，加上本文 1.3 焦点域、1.5 Z 序操作、1.6 resize、1.13 类型与层序、1.14 状态许可表、1.15 最大化、1.16 关闭两档、1.17 焦点模型 |
| 任务清单 | 本文 1.8，D 系列现无此刀 |
| 程序启动器 | D2 图标，加上本文 1.10 注册表 |
| 桌面面 | D2 容器与图标 |
| 文件管理器 | App 层第二位，依赖本文 2.4 列表视图与 3.5 文件封装 |
| 会话与设置 | 本文 1.12，推迟 |
| 终端 | D3 假源加 D4 真 PTY，本文 2.1 对齐 |

## 1. 桌面本体缺的抽象

### 1.1 弹层与菜单

缺口：一块画在所有兄弟之上、不受父容器几何约束、点外面会收回的覆盖层，以及它之上的菜单模型（菜单项、分隔、级联）。桌面右键菜单、窗口菜单、将来 App 的命令菜单都吃这一件。

对应物：概念参考 GNOME 的 PopupMenu 模块（Shell 侧菜单与子菜单的底座，gnome-architecture.md §2.3）。概念参考 Xfce 的 root menu（xfdesktop 右键出应用菜单，lightweight-and-classic-desktops.md §2.1）。

现状：visor 无。Dropdown 是树内长大画法，开合用 set_bounds 在树内扩缩（`include/visor/ui/widgets/dropdown/dropdown.hpp` 类注释），向下生长、不越父界、无外部按压收回机制（InputRouter 只管按下的捕获语义，`include/visor/ui/input_router.hpp`）。vesper 无。

处置：回填 visor。理由：浮层挂载点与外部按压收回是控件库语义，正好是 Dropdown 的第二步，菜单内容模型（项与命令）才归 vesper 自建。菜单是每个 App 都要吃的通用件，桌面私养一份等于长出第二个控件库。

### 1.2 剪贴板与文本选区

缺口：跨窗口的数据递送通道（显式复制粘贴与选中即有两套），以及控件侧的选区语义（拖选谁、选了什么）。

对应物：概念参考 X11 的 PRIMARY 与 CLIPBOARD 双选择（所有权模型，数据留在源端懒传输，desktop-infrastructure.md §6）。

现状：visor 无（对 include 与 src 全量检索 clip、copy、paste 无命中）。vesper 无代码，desktop-infrastructure.md §6 已拍「八件里唯一立刻取」，Xlib 的 selection 接口按系统库待遇不破零依赖红线。

处置：vesper 自建。理由：通道是桌面语义，控件库不该认识 X server。单进程内先养一个所有权模型的函数表，X11 selection 接线在宿主层，D6 分进程后同一形状升级为代理持有者。刀次口径（2026-10-17 对齐裁定）：PRIMARY 贴 D4 终端接线，拖选即写、中键粘贴；CLIPBOARD 的显式复制与拖放没有终端这个天然消费者，归任务栏或 WM 的刀次再接线（拖放本身在 desktop-infrastructure.md §6 就是拍推迟的）。Wayland 的 primary-selection 协议是这条裁定的协议级印证（window-and-session-theory.md §6.3）。

### 1.3 窗口级焦点域与 Alt+Tab

缺口：焦点路由要分两层。树内 Tab 遍历 visor 已有，窗口层「谁是活动窗口」没有。单画布单树之下，advance_focus 沿整树 draw 顺序走，Tab 会从一个窗口窜进另一个窗口。Alt+Tab 任务切换本身是 Z 序枚举的副产品，但前提是修饰键（见 1.4）与窗口焦点归属先立。

对应物：概念参考 Windows 95 的 Alt+Tab 任务切换（图标按窗口 Z 序排列，lightweight-and-classic-desktops.md §5.1）。

现状：visor 半个。单树焦点机制完整：focused、set_focused、advance_focus、焦点环绘制都在（`include/visor/ui/widgets/widget.hpp`、`src/ui/widgets/widget.cpp` 的 paint_focus_border），Tab 由 dispatcher 截走做遍历（`include/visor/ui/input_router.hpp`）。但焦点以子树为界的「焦点域」概念无。vesper 无。

处置：回填 visor。理由：Tab 跨窗口窜是控件库级缺陷，桌面在 dispatch 外围拦截 Tab 是补丁绕行。改法集中在 find_focusable_after 的边界判断，改动小、语义正，GTK 的每窗口焦点组是同一概念的成熟先例。

### 1.4 修饰键与全局快捷键

缺口：Alt+Tab、Alt+F4、Ctrl 加按键这类组合需要修饰键状态，核心事件里没有承载。

对应物：成熟桌面的事件模型都带修饰位（X11 按键事件的 state 掩码、Wayland 的 keyboard modifiers 事件，此处为通识判断，未附单一出处 URL）。概念参考 Flatpak 的 GlobalShortcuts portal（全局快捷键作为桌面服务的现代形状，desktop-infrastructure.md §8）。

现状：visor 无。KeyDown 只有 code 一个字段（`include/visor/bindings/events.hpp`）。本地证据链很完整：Cinux 线上的 WireKey 带 shift、ctrl、alt 三旗（`include/visor/bindings/cinux/event_wire.hpp`），translate_event 译成核心事件时丢弃，只取 ascii 或命名键（`src/bindings/cinux/event_wire.cpp`）。X11 侧 XLookupString 的 state 同样未进事件。vesper 无。

处置：回填 visor。理由：修饰键是输入事实，与滚轮同属事件 ABI 判例。晚加要回头改三个宿主的翻译层，早加只动 variant 一处加各翻译层一小段。

### 1.5 窗口 Z 序操作与最小化还原

缺口：树序即 Z 序，但没有 raise、lower 的重排接口。最小化等于 visible 开关加任务栏登记，还原要能回到原 Z 位。

对应物：概念参考 Windows 95 的任务栏（最小化的第一性论据是「看得见自己开着什么」，lightweight-and-classic-desktops.md §5.1）。

现状：visor 半个。set_visible 现成且翻转即报脏区（`src/ui/widgets/widget.cpp`，invalidate bounds），hit_test 按加入逆序取顶层命中（`include/visor/ui/widgets/widget.hpp`）。但 add_child 只追加，全公开面无子序重排接口。vesper 无。

处置：回填 visor。理由：Z 序是树的固有语义，窗口叠放是它的第一个真实用户，桌面层自己倒腾 children 数组要碰 Widget 内部状态，没有这条路。

### 1.6 窗口 resize

缺口：边缘拖拽改窗口尺寸，内容重排，终端窗口要联动 TIOCSWINSZ 并让子进程收 SIGWINCH。

对应物：概念参考 KWin 的移动缩放管理（kde-plasma-architecture.md §3.2）。终端侧的 winsize 链路见 terminal-emulator-internals.md §2（resize 时对 master 调 TIOCSWINSZ，内核向前台进程组发 SIGWINCH）。

现状：visor 半个。set_bounds 报新旧两框脏区（`src/ui/widgets/widget.cpp`），HBox 与 VBox 的 layout() 重排现成（`include/visor/ui/widgets/box/box.hpp`）。注意 X11WindowHost 的窗口在编译期固定为缩放后的画布尺寸、无 resize 事件（`include/visor/bindings/x11/x11_window_host.hpp` 注释「a resize would only add area nobody paints」），但那是宿主窗口的事，桌面画布是全屏的，窗口 resize 发生在树内，不受此限。vesper 无，D 系列只有 D1 拖动（移动），没有缩放刀。

处置：vesper 自建。理由：机制全是现成件的组合（边缘命中判定加 set_bounds 加子树 layout），缺的只是窗口侧边缘语义，属桌面层。排期建议贴 D4，因为终端是第一个真正在乎尺寸的窗口。

### 1.7 滚轮事件

缺口：滚动输入。列表视口、终端回滚、文本编辑都吃它。

对应物：概念参考 X11 的滚轮按钮语义（按钮 4 到 7 即滚轮步进，visor 自己的翻译层注释就是这么写的）。

现状：visor 无，且是显式预留的决定。Event 五成员无滚轮（`include/visor/bindings/events.hpp`），X11 翻译层把按钮 4 以上丢掉，注释明说「until a scroll layer exists」（`include/visor/bindings/x11/event_wire.hpp`、`src/bindings/x11/event_wire.cpp`）。vesper 无。

处置：回填 visor。理由：这是 visor 自己注释里预留的层，Event variant 加一个 PointerWheel 成员加宿主放行 4 与 5，事件 ABI 越早定越便宜。消费者（视口）可以后到。

### 1.8 任务栏

缺口：任务清单的可见面。轻量与经典桌面调研把任务清单列为四件硬底线之一，construction-order.md 修正一建议在 D2 与 D4 之间加一刀只读的窗口列表条。

对应物：概念参考 Windows 95 的任务栏（条目压在所有窗口之上，lightweight-and-classic-desktops.md §5.1）。概念参考 i3 的内置工作区条（第一版面板小到一条只读清单，两年后才分家，construction-order.md §5）。

现状：visor 无面板容器语义（Button 可拼列表但无「贴边置顶」概念，这本来也不该是控件库的事）。vesper 无，D 系列没有面板刀。

处置：vesper 自建。理由：桌面件。极简版是 Button 列表加焦点状态，窗口增减与焦点变化各报一次脏区，同时是 D1 窗口件语义的第一块试金石。

### 1.9 配置持久化

缺口：图标摆位、窗口默认大小、终端字号的存取。产生用户状态的第一刀是 D2。

对应物：概念参考 KDE 的 KConfig（KDE 第一批库类，格式仿 win.ini，construction-order.md §2）。概念参考 freedesktop 的 basedir 规格（放哪的约定，desktop-infrastructure.md §4.1）。

现状：visor 无文件 I/O（库面唯一的 ofstream 在 OffscreenHost 的测试转储里，`src/bindings/offscreen/offscreen_host.cpp`）。vesper 无，construction-order.md 修正二建议 D0 至 D1 进极简 ini 读写。

处置：vesper 自建。理由：定容 ini 读写器加 XDG 路径解析是小件，但必须先于产生用户状态的刀次存在，免得 D2 之后各刀各造存储格式。设置 GUI 完全不用急（见 1.12）。

### 1.10 App 注册表

缺口：App 口报名数据的形状：名字、图标、可执行物、是否显示（这一位要带成熟度语义，报名了但没长熟的 App 不上面板）。

对应物：概念参考 freedesktop 的 desktop entry（字段语义同时服务菜单项、启动器图标、自启条目，desktop-infrastructure.md §4.2）。

现状：visor 无关（注册表不是控件库的事）。vesper 半个：App 口概念已在交接文档 §1 拍板（报名字、给图标、造窗口内容、收生命周期回调），注册表内部结构未定。

处置：vesper 自建。理由：编译期注册模块表，字段语义照 desktop entry 设计，完整键集不解析，这是 desktop-infrastructure.md §4.2 已拍的半取路线，三梯队沿用同一套约定。「是否显示」位的成熟度语义抄 GNOME Core 名册的两条成文标准：下游接受度先于功能完备、维护断档比功能缺失更致命（app-suite-survey.md 启示节）。xdg-shell 的 set_app_id 取值约定正是 desktop entry 文件基名，与半取路线对上（window-and-session-theory.md §6.1）。

### 1.11 工作区

缺口：多工作区语义（一屏变多屏）。

对应物：概念参考 CDE 的工作区占用列表（窗口属于哪些工作区是可枚举属性，比无限虚拟大平面好实现，lightweight-and-classic-desktops.md §5.3）。

现状：visor 无（也不需要）。vesper 无，无排期。

处置：推迟。理由：单屏单进程期收益小，占用列表模型便宜到随时可加，先把语义写进词汇表即可。

### 1.12 会话与设置中心

缺口：登录、恢复、登出、电源动作（会话），外观、输入、显示的设置 GUI（设置中心）。

对应物：概念参考 Xfce 的 xfce4-session 与 xfce4-settings（概念件清单，lightweight-and-classic-desktops.md §2.1）。建设顺序的律六：设置 GUI 中心天然晚到，Xfce 距立项十二年（construction-order.md §6）。概念参考 XSMP 的 previous-ID 会话身份（恢复的全量协议机器：双向消息、两相保存、interact 排队，window-and-session-theory.md §7.3）。

现状：均无。会话在单进程手动拉起期由启动脚本兼任，desktop-infrastructure.md §2 的拍板是「舍整个 systemd，取清单驱动的自启」，Cinux 侧由 init 线程直接拉桌面。恢复的最小形状，理论稿已裁定：一份快照加一次回放，单进程期快照就是窗口树加 App 注册表的序列化，与 CDE 占用列表同属「WM 自己就能存的廉价事实」一端，XSMP 的协议机器不要（window-and-session-theory.md §7.3）。

处置：推迟。理由：两件都是「有了更好、没有也能用」的生态件（律八），现阶段的会话等价物是 D0 的启动装配，设置中心等配置持久化积累出足够条目再立。恢复的形状已定但排期仍晚：快照回放吃 1.9 的持久化件，等窗口摆位值得存了顺手做；「子程序自存自报」是 D6 之后的事，previous-ID 身份模型记进 D6 口袋（window-and-session-theory.md §7.3）。

以下 1.13 至 1.19 与第 2 节的 2.7 至 2.10 为同日增补（两篇草稿到稿后缝合，另含完整性审查点名两条），编号接排不插队，旧条目编号不动。

### 1.13 窗口类型位与层序

缺口：App 报名数据里没有窗口类型位，层序（桌面面、普通窗、任务栏与浮层谁压谁）没有成文规则，「哪类窗口永不取焦点」也就无从谈起。tooltip、菜单类瞬时窗与普通窗在装饰、叠放、焦点态度上应当区别对待，现在树里一律平等。

对应物：概念参考 EWMH 的 WINDOW_TYPE 类型学（十四型按功能分型，WM 对同型窗口应用一致的装饰与行为，window-and-session-theory.md §2.1）。概念参考 EWMH 的分层叠放序（五层自底向上，raise 与 lower 的自由区只在同层内，window-and-session-theory.md §5）。概念参考 layer-shell 的 keyboard interactivity 三档（每层对键盘的态度：none、exclusive、on_demand，window-and-session-theory.md §6.2）。

现状：visor 无类型概念，树内兄弟一律平等，hit_test 按加入逆序取顶层命中（`include/visor/ui/widgets/widget.hpp`）。vesper 半个：交接文档 §1 的 App 口只拍报名字、给图标、造窗口内容、收回调，无类型位；理论稿 §8.1 建议类型位从第一天进报名数据。

处置：vesper 自建。理由：类型位是桌面语义，装饰、层序、焦点态度全从它派生，控件树只需要多一个字段。单色第一版不需要十四型，三型够用（普通窗、瞬时窗、桌面面），D2「桌面面不是窗口」的裁定（理论稿 §8.4）是第一条类型判例。

### 1.14 状态与许可两张表

缺口：窗口对象缺两套正交的表：「现在怎样」的事实表（maximized、iconic、focused）与「用户能对它做什么」的许可表（可移动、可缩放、可关闭）。任务栏与窗口菜单要读许可表决定亮哪些操作，事实表随交互自己变。混在一组旗标里，任务栏就得猜。

对应物：概念参考 EWMH 的 state 与 allowed actions 分离（状态是 WM 写的事实，许可是 WM 写的政策，两张表两个原子，window-and-session-theory.md §2.5）。

现状：visor 无（Widget 只有 visible、enabled 散旗，`include/visor/ui/widgets/widget.hpp`，不成体系）。vesper 无。

处置：vesper 自建。理由：纯桌面语义，控件库无感。单进程期许可表可以是 App 报名数据里的编译期常量，事实表就是窗口对象的几个字段；分表要趁早，混着长再拆就得动任务栏。

### 1.15 最大化与还原几何

缺口：最大化语义：Normal 的子状态而非独立态，目标对齐 workarea（扣掉任务栏后的区域）而非整屏，还原要能回到 pre-maximize 的记忆几何，横竖两个方向可独立最大化。

对应物：概念参考 EWMH 的最大化（非 ICCCM 正式态，早期草案的 ZoomedState 后删除，还原几何由 WM 负责，window-and-session-theory.md §2.2 与 §4）。概念参考 EWMH 的 workarea 与 struts（面板声明四边保留量，最大化与桌面图标都对齐扣除后的区域，window-and-session-theory.md §2.3）。

现状：visor 无（也不需要，set_bounds 可当落点，`src/ui/widgets/widget.cpp`）。vesper 无，无排期。

处置：vesper 自建，排期贴任务栏刀之后。理由：workarea 以任务栏存在为前提（与 construction-order.md 修正一的任务栏刀咬合），记忆几何只是两个字段，但任务栏立起之前最大化等于全屏，没有可试的语义。

### 1.16 关闭两档与探活口袋

缺口：关窗语义要分两档：请示档（on_close 回调，App 可拦下，例如文本编辑器有未存改动时）与武力档（强制销毁，窗口连同 PTY 子进程一起收）。D6 子程序后还要有探活（子程序无响应时桌面能判断并处置）。

对应物：概念参考 ICCCM 的 WM_DELETE_WINDOW 两档关闭（报名协议的走 ClientMessage 请示，没报名的武力断连，礼貌与武力 1994 年就分开，window-and-session-theory.md §1）。概念参考 EWMH 的 _NET_WM_PING（探活，无响应凭 PID 加机器名杀进程，window-and-session-theory.md §4）。

现状：visor 无窗口生命周期概念（控件 set_visible 翻转即完，`src/ui/widgets/widget.cpp`）。vesper 半个：App 口已拍「收生命周期回调」（交接文档 §1），回调的档位与武力档未定。

处置：vesper 自建。理由：生命周期是 App 口的事。两个消费者已排上队：文本编辑器的 dirty 位拦关窗与终端关窗收 PTY 子进程（app-suite-survey.md §6，四件 App 各逼一条 App 口语义）。探活记进 D6 口袋，单进程期用不上。

### 1.17 焦点模型与激活语义

缺口：桌面层焦点模型未拍：click-to-focus 还是 focus-follows-mouse、焦点转移要不要带来源字段（用户动作还是子程序请求）、激活（抬升加取焦点）是否原子、键盘焦点与指针命中是否保持两个独立事实。1.3 立的是树内焦点域，这一条立的是桌面政策，两层互补。

对应物：概念参考 ICCCM 的 WM_TAKE_FOCUS 让渡模型（焦点是所有权让渡，WM 问、客户端答，四模型表，window-and-session-theory.md §1.1）。概念参考 EWMH 的 source indication 与 USER_TIME（谁有权改焦点分级，定时器弹的不该偷焦点，window-and-session-theory.md §2.4）。概念参考 i3 的 focus-follows-mouse（FFM 的 mouse warping 对账问题，反面证据，window-and-session-theory.md §3）。

现状：visor 侧键盘焦点与指针命中在结构上已分离：指针事件按命中分派、键盘按焦点走，互不干涉（`include/visor/ui/input_router.hpp`）。桌面层的活动窗口变量与来源字段无。vesper 无，理论稿 §8.1 已给拍板建议。

处置：vesper 自建。理由：理论稿 §8.1 的建议可直接采纳：click-to-focus（与捕获语义咬合，按下者持有，拖选不抢焦点）、来源字段从第一天进接口（单进程期只有用户动作一种取值，字段留着）、激活原子性照 EWMH 2.8 的组合动作清单验收；FFM 显式不做（理论稿 §8.2 负面清单），但键盘焦点与指针命中保持两个独立事实，将来加 FFM 是策略件不是结构返工。

### 1.18 双击手势

缺口：双击判定的手势层：两次 PointerDown 的时空窗判定，与单击选中区分。D2 桌面图标的激活语义吃它。

对应物：图标双击激活是成熟桌面的通识约定（此处为通识判断，未附单一出处 URL）；判例价值在单击与双击的区分：选中是视觉事实、不夺键盘焦点，激活才是开窗加抬升加取焦点的组合动作（window-and-session-theory.md §8.4）。

现状：visor 无，且是显式预留：events.hpp 的 Event 注释明说手势是五事件之上的将来层、不是新的表项（`include/visor/bindings/events.hpp`），`tests/gesture_tests.cpp` 测的是捕获语义的投递、不含双击。vesper 无，D2 刀面已写「双击判定」（交接文档 §6）。

处置：回填 visor，D2 前与上游拍。理由：手势层长在事件投递之上，是控件库语义；construction-order.md 修正三的结论同款，即按交接文档 §8 的倾向回 visor 上游提案，与「手势是五事件之上的将来层」的既有拍板对齐。桌面私养一份双击，将来的列表视图、树控件就得再养第二份。提案时连带拍时间窗（clock_ms 现成，`include/visor/bindings/host.hpp`）与「单击是否等双击窗过期才触发」的延迟取舍。

### 1.19 容器子表容量

缺口：`Widget::kMaxChildren = 8` 定容子表，桌面上图标加窗口轻松超八，Desktop 容器要么内部嵌套分组、要么上游扩容。要早拍，因为它直接决定 D2 Desktop 容器的形状。

对应物：概念参考 KDE Plasma 的 Containment 嵌套（桌面与面板是同一种容器，容器里再装容器，Panel 自己也是 Applet，kde-plasma-architecture.md §3.1）。该文启示节的现成答案：桌面之下摆图标栏分组，而不是一张无限扁表，容器嵌容器正好回答容量问题（kde-plasma-architecture.md §6）。

现状：visor 是定容 8（`include/visor/ui/widgets/widget.hpp` 的 kMaxChildren 与 children_ 定长数组，`src/ui/widgets/widget.cpp` 的 add_child 满员即拒）。vesper 无代码，交接文档 §9.2 已把它列为未决问题，§6 的 D2 刀面写明「子表容量问题在此拍」。

处置：D2 前拍，倾向嵌套容器、上游扩容为备选。理由：嵌套走现有定容纪律，不破库的零堆分配风格；Containment 判例说明分组容器本身有语义（图标分区、任务栏分组），不是权宜。若 D2 实测嵌套层次过深导致命中与绘制绕路，再回上游议扩容。扩容是控件库 ABI 判断，不轻动。

## 2. App 层缺的抽象

### 2.1 终端网格控件

缺口：D3 与 D4 的主体。列在这里只为对齐全景，它是已排期件。

对应物：概念参考 VTE 的终端控件边界（控件不是应用，填进窗口就能用，terminal-emulator-internals.md §1）。概念参考 Konsole 的 Session、ScreenWindow、TerminalDisplay 三层（会话与视图分离）。

现状：visor 无（终端不是通用控件，但这不妨碍它长在 vesper 侧，之后再回捐）。零件半个：RingQueue 可当回滚行队列（`include/visor/base/containers/ring_queue.hpp`），MonoFont 位图字形现成（`include/visor/draw/mono_font.hpp`），SGR 属性位映单色黑白在 internals 草稿已论证。vesper 无代码。

处置：vesper 自建（已排期 D3 假源、D4 真 PTY）。唯一建议现在拍死的细节：列号从第一天按显示宽度计（wcwidth 轨道），不按 cell 下标，internals 草稿 §5 的原话是「给中文留轨」。

### 2.2 滚动视口

缺口：viewport 控件。内容大于可见区，滚动条或滚轮驱动视口窗，终端回滚、列表、多行编辑三处共同的前置。

对应物：概念参考 alacritty 的回滚视口（vi mode 在回滚区移动，history 默认一万行，terminal-emulator-internals.md §1 与 §4）。

现状：visor 半个。TextBox 的 fit_window 是单行滑动窗思想的种子（`include/visor/ui/widgets/textbox/textbox.hpp`），无通用视口件。前置依赖是 1.7 的滚轮事件，目前缺失。渲染侧条件反而现成：render_window 本来就是「按窗口重放整棵树」（`include/visor/ui/run_loop.hpp`），视口只是把窗口从条带推广到任意矩形。

处置：回填 visor。理由：视口是控件库语义，三处消费者都吃，私养三份最贵。排期贴滚轮事件之后。

### 2.3 多行文本编辑

缺口：文本编辑器 App 的底座：行存储、折行、光标上下移动、编辑与视口联动。

对应物：概念参考 KDE 的 KTextEditor（Frameworks 里的编辑器控件，Kate 是它的消费者，kde-plasma-architecture.md §2.1）。概念参考 GNOME 的 Text Editor 新写上位判例（替换 gedit，说明编辑器控件可以独立于应用重写，gnome-architecture.md §4.2）。

现状：visor 半个偏无。TextBox 是单行 64 字节 ASCII 定容缓冲（kTextBoxCapacity 与 chars_，`include/visor/ui/widgets/textbox/textbox.hpp`），类注释明说宽字符输入是同一存储之上的后来层。base 层 UTF-8 解码现成（decode_utf8_next，`include/visor/base/text/utf8.hpp`），MonoFont 有 line_height 作换行步进（`include/visor/draw/mono_font.hpp`）。

处置：回填 visor。理由：与 TextBox 同族，定容无堆纪律可以延续，控件库件桌面私养会养出双份。排期贴文本编辑器 App 立项。

### 2.4 列表视图

缺口：定容列表加选择态，文件管理器的主体容器。

对应物：概念参考 classic Mac OS 的 Finder 空间式窗口（一文件夹一窗口，位置即记忆，1bpp 下几乎免费，lightweight-and-classic-desktops.md §5.2）。

现状：visor 半个。RadioGroup 是定容单选组（`include/visor/ui/widgets/radio/radio.hpp`），Dropdown 定容 8 项（kDropdownCapacity），都无滚动与视口，容量语义可参照但整件无。

处置：vesper 先建，定型后回填 visor。理由：与定容纪律对齐，先在文件管理器这个真实消费者手里长形状，比凭空进库稳。这一条是清单里唯一建议「先私养后回捐」的，因为它的形状（图标网格还是文字列表）还没被真实需求裁定。第二个消费者已排上队：系统监视器的进程表（app-suite-survey.md §5），一个要图标网格，一个要文字行，形状裁定正好各交一票。

### 2.5 文件对话框

缺口：打开与保存的跨 App 通用对话框。

对应物：概念参考 Flatpak 的 FileChooser portal（应用向桌面请求代办的原型，desktop-infrastructure.md §8）。

现状：无。单进程期「对话框」就是一次函数调用加一个浮层（依赖 1.1），portal 的请求代理形状是 D6 之后同一通道上的同构物。

处置：推迟。理由：文件管理器有列表视图与目录枚举之后再做不迟，portal 概念先记进 D6 的设计口袋。

### 2.6 输入法文本入口

缺口：preedit 与 commit 两段语义的文本入口，中文输入的接口形状。

对应物：概念参考 IBus 的 input context（preedit、候选、commit 三段，desktop-infrastructure.md §7）。

现状：visor 的文字输入是 KeyDown 码点约定（events.hpp 的 Event 注释明说 text input 是 KeyDown 之上的后来层），无双段语义。vesper 无。

处置：推迟。理由：desktop-infrastructure.md §7 已拍「短期舍实现，中期取概念」，终端控件定稿时给文字入口预留 preedit 与 commit 的参数位即可，迷你拼音过滤器是将来的教学判例。

### 2.7 系统监视器取数表

缺口：进程表与负载数据的取数表：把 /proc 的 loadavg、进程目录、stat 变成定容结构的读数接口。系统监视器 App 的数据底座，表形状要独立于 UI。

对应物：概念参考 libgtop 的表底座（GNOME 系统监视器的取数库；KDE 侧的 libksysguard 被 plasma-systemmonitor 与前代 KSysGuard 两代 UI 共用，UI 换代、表不动，app-suite-survey.md §1 与启示节）。

现状：visor 无（不适用）。vesper 无；3.5 只管文件字节读写，不含 /proc 语义的解析。app-suite-survey.md §5 已把它列为监视器的额外依赖面。

处置：vesper 自建。理由：读 /proc 是 Linux 侧系统调用的薄封装，Cinux 侧将来换同名伪文件（app-suite-survey.md §5），表形状三梯队沿用。这也是教学论据的现成教具：最像「系统」的 App，数据源根本不在 GUI 层，在内核的伪文件系统里（app-suite-survey.md §5 补充二）。

### 2.8 归档与解压

缺口：压缩包的列出与解包件，文件管理器将来看见 zip 与 tar 时要么能开要么能转交。

对应物：概念参考 Xarchiver 的外部工具前端（本体只是壳，tar 与 zip 是外部程序，格式取决于系统里装没装工具，app-suite-survey.md §3）。概念参考 GNOME 的 gnome-autoar 吸收判例（file-roller 出局、Files 自己能开压缩包，归档类别被文件管理器吃掉，app-suite-survey.md §2）。

现状：无。spawn（3.1）已是 D4 与 D6 的主线件，外部工具薄壳与自写最小 zip 读取器两模型二选一（app-suite-survey.md §5）。

处置：推迟。理由：外部薄壳优先，它是零第三方依赖纪律下唯一不用自写解码器的形状，顺带是 spawn 的教学判例（app-suite-survey.md 启示节）；贴文件管理器稳定之后。GNOME 的吸收判例说明类别能并就并，并掉一件少养一件。

### 2.9 图片查看与位图解码

缺口：图片查看 App 与它的底座位图解码器（PNG 一类的最小解码件）。

对应物：概念参考 GNOME 的 Loupe（Rust 写，图像解码靠 glycin，格式支持随解码栈走，app-suite-survey.md §2）。概念参考 Ristretto 的 gdk-pixbuf（解码层可替换，更多格式靠可选 loader，app-suite-survey.md §3）。

现状：visor 只有 MonoBitmap（画图标够用），RGB565 彩色在 visor 第三站，分块探针过前渲染接口不冻结（交接文档 §4）。vesper 无。

处置：推迟。理由：1bpp 黑白下看图价值趋近于零，解码器属「二档必须自写」的件（app-suite-survey.md §0），等 RGB565 站再动（app-suite-survey.md §4）。

### 2.10 音频输出

缺口：音频输出通道。媒体播放与小游戏的共同前置，vesper 现在一条都没有。

对应物：概念参考 Parole 的 GStreamer 底座（媒体播放坐在解码栈上，app-suite-survey.md §3）。desktop-infrastructure.md §3 已把音频栈整体列为将来站。

现状：无。宿主表三口不含音频；事件五件套与 MonoBitmap 倒是现成，棋盘类小游戏（扫雷、贪吃蛇）不需要音频也能立（app-suite-survey.md §5）。

处置：推迟。理由：解码栈不存在；小游戏是音频站的触发件不是等待件，先做静音棋盘类，音频由真需求逼出来（app-suite-survey.md §4 与 §7）。

## 3. GUI 无关 base 缺的抽象

### 3.1 进程启动与 PTY

缺口：forkpty 加 execvp 的 linux_spawn（D4），以及 D6b 的任意程序拉起加子进程回收。

对应物：概念参考 D-Bus 的服务激活（报一个名字，管家把真进程变出来，再把最初的输入递过去，desktop-infrastructure.md §1）。forkpty 语义见 man7 手册（terminal-emulator-internals.md §2 转引）。

现状：visor 无（spawn、fork、exec、pty 全量检索无命中），宿主表只有三口。血统清单里旧仓 `host/posix_spawn.cpp` 在可抄名单（交接文档 §3）。vesper 无代码。

处置：vesper 自建。理由：D4 先抄 linux_spawn 一条，第四口议题与 visor stage-4 联动拍（交接文档 §9.6），spawn 是宿主边界的扩展，不是控件库的事。

### 3.2 信号与作业控制

缺口：SIGWINCH（终端 resize 联动）、SIGCHLD（子进程回收）、SIGINT 与 SIGTERM（优雅退出）。

对应物：概念参考 The TTY demystified 的行规程（ISIG 把 ^C 变 SIGINT 发给前台进程组，终端侧无感知，terminal-emulator-internals.md §2）。

现状：visor 无信号概念（控件库不适用）。vesper 无。Cinux 侧信号是站 21 的前置（交接文档 §7）。

处置：vesper 自建。理由：Linux 侧 D4 收刀时就要 SIGCHLD 与 SIGWINCH，Cinux 侧等内核站到位，两侧语义同形。

### 3.3 多路等待编排

缺口：poll 编排宿主 X 连接加各 PTY master，谁有货醒谁，队列空则睡。

对应物：成熟桌面把这件事收进各自的主循环（GNOME 的 GLib 主循环、Qt 的事件循环，此处为通识判断，未附单一出处 URL）。

现状：visor 半个。run_dirty 是单宿主队列的一轮泵（`include/visor/ui/run_loop.hpp`），轮次由调用者驱动，装配样板是固定睡 15ms（`examples/x11_window.cpp`）。交接文档 §6 的 D4 已写明「事件循环扩成 poll 编排」。

处置：vesper 自建。理由：多源等待天然住在桌面与宿主之间，run_dirty 保持不动，在它外面再包一层，这正是交接文档既定的切法。

### 3.4 定时器抽象

缺口：deadline 表。光标闪烁、任务栏时钟、自动隐藏、重复触发都要它。还要注销语义：窗口关了，它登记的 deadline 要能撤。

对应物：概念参考 KWin 的 RenderLoop（调度从定时器驱动换成反馈驱动的判例，说明调度策略值得独立成件，kde-plasma-architecture.md §4.4）。

现状：visor 半个。clock_ms 毫钟现成且约定按无符号减法算时长（`include/visor/bindings/host.hpp`），定时器表无（timer、schedule 全量检索无命中）。

处置：vesper 自建。理由：deadline 数组加 poll 超时一体化是 3.3 的顺手带，一件小刀两处受益。注销语义由系统监视器逼出：App 口生命周期回调里要注销刷新节拍（app-suite-survey.md §6），登记时发句柄、注销凭句柄撤，形状一次定对。

### 3.5 文件系统封装

缺口：目录枚举、读文件进定容缓冲、写文件。文件管理器与配置持久化的共同前置。

对应物：概念参考 freedesktop 的 basedir（放哪）与 shared-mime-info 的 glob 思路（类型判断，desktop-infrastructure.md §4.1 与 §4.4）。

现状：visor 无（库面无文件 I/O，见 1.9 的唯一例外）。vesper 无。

处置：vesper 自建。理由：dirent 加定容读写的薄封装就够教学用，Cinux 侧同一形状换它的文件系统调用。类型表自养一张小 glob 表，magic 不做，这是 desktop-infrastructure.md §4.4 的半取拍板。

### 3.6 环境变量

缺口：DISPLAY 解析（现由 Xlib 代劳）、TERM 设置（forkpty 子进程要带对）、XDG 各变量的读取。

对应物：概念参考 freedesktop 的 basedir 变量族（desktop-infrastructure.md §4.1）。概念参考 D-Bus 的 UpdateActivationEnvironment（显示变量要专门灌进激活环境的判例，desktop-infrastructure.md §1）。

现状：visor 无显式环境接口（getenv 检索无命中，Xlib 内部自读 DISPLAY）。vesper 无。

处置：vesper 自建。理由：getenv 与 setenv 的薄函数加一张常量名表，小到不值得回填 visor，但 D4 设 TERM 与 1.9 解析 XDG 路径都直接吃它。

## 4. 工作量估计

刀级口径：小刀是一两天内一刀带验收的件，中刀是数天且牵多文件或带协议判断的件，大刀是要拆成两刀以上、或牵全宿主联动的件。

| 条目 | 件 | 刀级 | 建议刀次 |
|---|---|---|---|
| 1.1 | 弹层与菜单 | 中刀 | D2 前回填 visor（浮层），菜单模型随 D2 右键菜单 |
| 1.2 | 剪贴板与选区 | 中刀 | PRIMARY 贴 D4 终端；CLIPBOARD 与拖放归任务栏或 WM 刀次 |
| 1.3 | 窗口焦点域 | 小刀 | D1 前回填 visor |
| 1.4 | 修饰键 | 小刀偏中 | 与 1.7 同一刀回填（三宿主翻译层都动） |
| 1.5 | Z 序重排 | 小刀 | D1 前回填 visor |
| 1.6 | 窗口 resize | 中刀 | 贴 D4（终端 winsize 联动） |
| 1.7 | 滚轮事件 | 小刀 | 尽早回填（事件 ABI） |
| 1.8 | 任务栏极简条 | 小刀 | D2 与 D4 之间新刀 |
| 1.9 | 配置持久化 | 小刀 | D0 至 D1 |
| 1.10 | App 注册表 | 小刀 | D2 前 |
| 1.11 | 工作区 | 小刀（真做时） | 推迟 |
| 1.12 | 会话与设置中心 | 中刀起 | 推迟（恢复形状已定：快照回放） |
| 1.13 | 窗口类型位与层序 | 小刀 | D2 前（随 1.10 注册表一起拍） |
| 1.14 | 状态与许可两张表 | 小刀 | D1 前定字段，任务栏刀落地 |
| 1.15 | 最大化与还原 | 小刀 | 任务栏刀之后 |
| 1.16 | 关闭两档 | 小刀 | 随 App 口立，D4 终端先吃；探活记 D6 口袋 |
| 1.17 | 焦点模型与激活 | 小刀 | D1 前拍板（拖动即激活是第一判例） |
| 1.18 | 双击手势 | 小刀 | D2 前与 visor 上游拍 |
| 1.19 | 容器子表容量 | 小刀偏中 | D2 拍（嵌套优先，扩容备选） |
| 2.1 | 终端网格 | 大刀 | D3 加 D4（已排期） |
| 2.2 | 滚动视口 | 中刀 | 1.7 之后回填 visor |
| 2.3 | 多行编辑 | 大刀 | 文本编辑器 App 立项时回填 visor |
| 2.4 | 列表视图 | 中刀 | 文件管理器 App 内先建 |
| 2.5 | 文件对话框 | 中刀 | 推迟 |
| 2.6 | 输入法入口 | 小刀（只留形状） | 终端定稿时，实现推迟 |
| 2.7 | 监视器取数表 | 小刀 | 监视器 App 抢跑时（建议 D2 前后） |
| 2.8 | 归档与解压 | 小刀 | 推迟，文件管理器稳定后 |
| 2.9 | 图片查看与解码 | 大刀（解码器） | 推迟，RGB565 站之后 |
| 2.10 | 音频输出 | 大刀（新栈） | 推迟，音频站（小游戏触发） |
| 3.1 | spawn 与 PTY | 中刀 | D4 一小步，D6b 一中步 |
| 3.2 | 信号与作业控制 | 中刀 | D4 |
| 3.3 | poll 编排 | 中刀 | D4 |
| 3.4 | 定时器表 | 小刀 | D4 顺手（注销语义随监视器逼出） |
| 3.5 | 文件系统封装 | 中刀 | D2 前（配置要读写），文件管理器前补枚举 |
| 3.6 | 环境变量 | 小刀 | D4 |

合计三十五条（初版二十四条，同日修订缝合两篇到稿草稿增补十一条），其中回填 visor 八条（1.1、1.3、1.4、1.5、1.7、1.18、2.2、2.3），推迟八条（1.11、1.12、2.5、2.6、2.8、2.9、2.10 及 2.4 的回捐半步），其余 vesper 自建。回填件集中在三簇：事件 ABI 一簇（1.4、1.7 加将来的 text-input 形状）宜一刀提，树语义一簇（1.1、1.3、1.5）宜在 D1 开工前定，因为 D1 的 WindowFrame 马上要吃 Z 序与焦点；手势与容量一簇（1.18、1.19）D2 前与上游拍，因为 D2 的图标双击与 Desktop 容器直接吃。增补件里窗口语义五条（1.13 至 1.17）全是小刀，合起来是 D1、D2 与任务栏刀之间的窗口件语义包；App 侧四条（2.7 至 2.10）三条推迟、一条小刀，不阻塞主线。

## 5. 对 vesper 的启示

- 事件 ABI 是全清单最急的一簇。滚轮、修饰键、将来 text-input 的形状，都是 Event variant 的扩员判例，一动全宿主翻译层跟着动。GNOME 用十年扩展生态折损换来「协议头留版本字段」的教训（gnome-architecture.md），vesper 的同构物就是现在把事件表定宽一点。概念参考 GNOME Shell 扩展的版本闸。
- 回填与自建的分界线可以一句话立住：输入事实与树语义归 visor，桌面语义与宿主接线归 vesper。剪贴板、任务栏、spawn 在这条线的右侧，焦点域、滚轮、弹层在左侧。概念参考 GNOME 的 GTK 与 libadwaita 分层。
- 半成品的组合路径比新件便宜。set_bounds 双框报脏、fit_window 滑窗、render_window 窗口重放、RingQueue 定容队列，这四件现成半成品分别指向 resize、视口、弹层与回滚，清单里一半的中刀其实是它们的组合而非新造。
- D 系列当前缺两刀：面板与配置持久化，construction-order.md 修正一与修正二在这份清单里得到独立复核，结论一致。此外建议把「resize」明写进 D4 的刀面，现在它只隐含在终端 winsize 联动里。
- 教学论据的投影，账目算清楚：GUI 无关 base 占六条（3.1 至 3.6），桌面本体十二条里另有两条，即剪贴板（1.2）与修饰键（1.4），本质是操作系统服务的接线，合计八条，初版二十四条里恰三分之一。常被一并点名的四件（剪贴板、修饰键、定时器、环境变量）分属两层不重复计：剪贴板与修饰键计在桌面本体侧，定时器（3.4）与环境变量（3.6）已计在 base 六条之内。「GUI 离操作系统很远」这条论据，在缺件清单上就是「三分之一的缺口根本不长在 GUI 层」。同日增补保持同向：监视器取数表（2.7，读 /proc）又是一件数据源在内核伪文件系统里的件。概念参考 PipeWire 的图式处理引擎（整条音频链没有窗口的同构论据）。
- 单进程把协议折叠成函数调用：ICCCM 与 EWMH 的线格式（atom、ClientMessage）对 vesper 没有意义，但它们整理出的语义清单（类型、状态、许可、尺寸提示、焦点让渡）是窗口件数据结构的一手设计参照，借语义不搬线格式（window-and-session-theory.md §0 方法论与 §8.2 负面清单）。概念参考 EWMH 的语义清单。
- App 建设不阻塞 D 系列主线：第一波四件 App 全部不需要新宿主能力。终端吃 PTY、文件管理器吃文件系统、监视器吃 /proc 与定时器、编辑器吃纯控件，宿主表三口一个不动（app-suite-survey.md §5）。App 波次与 D 系列宿主刀是平行线，D6 拆进程时才出现第一个真实节奏问题。

## 6. 参考来源

本地材料（本次核对的一手依据）：

- /home/charliechen/visor 工作树，HEAD 9294df8（2026-10-16）。核对文件：include/visor/ui/widgets/widget.hpp、textbox/textbox.hpp、dropdown/dropdown.hpp、radio/radio.hpp、box/box.hpp、include/visor/ui/input_router.hpp、run_loop.hpp、dirty_regions.hpp、include/visor/bindings/events.hpp、host.hpp、x11/x11_window_host.hpp、x11/event_wire.hpp、cinux/event_wire.hpp、include/visor/base/containers/ring_queue.hpp、base/text/utf8.hpp、include/visor/draw/mono_font.hpp、src/ui/widgets/widget.cpp、src/bindings/cinux/event_wire.cpp、src/bindings/offscreen/offscreen_host.cpp、examples/x11_window.cpp、tests/gesture_tests.cpp。支撑第 1 至 3 节全部「现状」判断。
- /home/charliechen/vesper/pack-26-desktop-handover.md。支撑第 0 节刀序、第 1.6、1.10、2.1、3.1、3.3 节的既定拍板。
- documents/research/kde-plasma-architecture.md。支撑 1.6（KWin）、2.3（KTextEditor）、3.4（RenderLoop）。
- documents/research/gnome-architecture.md。支撑 1.1（PopupMenu）、2.3（Text Editor 判例）、第 5 节版本闸与分层。
- documents/research/lightweight-and-classic-desktops.md。支撑第 0 节八件概念件表、1.3 与 1.5 与 1.8（Windows 95）、1.11（CDE）、2.4（Finder）。
- documents/research/desktop-infrastructure.md。支撑 1.2（X11 双选择）、1.9 与 3.5 与 3.6（basedir、mime）、1.10（desktop entry）、2.5 与 1.4（portal、GlobalShortcuts）、2.6（IBus）、3.1（服务激活）、第 5 节（PipeWire 论据）。
- documents/research/terminal-emulator-internals.md。支撑 1.6（TIOCSWINSZ 与 SIGWINCH）、2.1（VTE 边界、wcwidth）、2.2（alacritty 回滚）、3.1（forkpty）、3.2（行规程）。
- documents/research/construction-order.md。支撑 1.8 与 1.9（修正一与修正二）、1.12（律六律八）、1.18（修正三的双击上游提案）、第 5 节。
- documents/research/window-and-session-theory.md（2026-10-17 到稿，同日缝合）。支撑 1.2（刀次口径与 primary-selection 印证）、1.10（set_app_id 约定）、1.12（恢复最小形状与 XSMP 口袋）、1.13 至 1.17、1.18（单击与双击的区分）、第 5 节（协议折叠、App 不阻塞主线的前半）。
- documents/research/app-suite-survey.md（2026-10-17 到稿，同日缝合）。支撑 1.10（成熟度语义）、1.16（关窗拦截与收子进程）、2.4（进程表第二消费者）、2.7 至 2.10、3.4（注销语义）、第 5 节（App 不阻塞主线的后半）。

草稿转引的网络出处（支撑各对应物条目，原始 URL 见各草稿参考来源节）：

- https://www.freedesktop.org/wiki/Specifications/ClipboardsWiki/ 支撑 1.2
- https://devblogs.microsoft.com/oldnewthing/20031020-00/?p=42093 与 https://socket3.wordpress.com/2018/02/03/designing-windows-95s-user-interface 支撑 1.3、1.5、1.8
- https://flatpak.github.io/xdg-desktop-portal/docs/ 支撑 1.4、2.5
- https://gjs.guide/extensions/overview/architecture.html 支撑 1.1
- https://docs.xfce.org/xfce/xfdesktop/usage 支撑 1.1
- https://kde.org/announcements/1-2-3/2.0/ 与 https://20years.kde.org/book/20yearsofKDE.pdf 支撑 1.9、3.1（经 construction-order.md 转引）
- https://specifications.freedesktop.org/basedir-spec/latest/ 与 https://specifications.freedesktop.org/desktop-entry-spec/latest/ 与 https://specifications.freedesktop.org/shared-mime-info-spec/latest/ 支撑 1.9、1.10、3.5、3.6
- https://docs.oracle.com/cd/E19455-01/806-1362/owtocde-46/index.html 支撑 1.11
- https://en.wikipedia.org/wiki/GNOME_Terminal 与 https://sw.kovidgoyal.net/kitty/conf/ 与 https://raw.githubusercontent.com/alacritty/alacritty/master/docs/features.md 支撑 2.1、2.2
- https://en.wikipedia.org/wiki/Intelligent_Input_Bus 支撑 2.6
- https://www.linusakesson.net/programming/tty/ 与 https://man7.org/linux/man-pages/man3/forkpty.3.html 支撑 3.1、3.2
- https://blog.vladzahorodnii.com/2020/12/10/compositing-scheduling-in-kwin-past-now-and-future 支撑 3.4
- https://pipewire.org/ 支撑第 5 节
- https://tronche.com/gui/x/icccm/ 与 https://specifications.freedesktop.org/wm-spec/latest/ 支撑 1.13 至 1.17（经 window-and-session-theory.md 转引）
- https://wayland.app/protocols/primary-selection-unstable-v1 与 https://wayland.app/protocols/wlr-layer-shell-unstable-v1 与 https://wayland.app/protocols/xdg-shell 支撑 1.2、1.10、1.13（经 window-and-session-theory.md 转引）
- https://xorg.freedesktop.org/releases/X11R7.7/doc/libSM/xsmp.html 支撑 1.12（经 window-and-session-theory.md 转引）
- https://discuss.kde.org/t/a-kde-gear-proposal-splitting-core-apps-from-gear-kde-core-app-group/13794 支撑 1.10、2.7 的判据（经 app-suite-survey.md 转引）
- https://www.linuxfromscratch.org/blfs/view/13.0-systemd/gnome/gnome-system-monitor.html 与 https://en.wikipedia.org/wiki/Xarchiver 支撑 2.7、2.8（经 app-suite-survey.md 转引）
