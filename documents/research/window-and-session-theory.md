# 窗口管理与会话理论：ICCCM、EWMH、Wayland 协议族与登录会话

> 调研日期 2026-10-17 · 服务对象：vesper 手搓教学桌面 · 来源：网络检索加本地材料

## 0. 这份文档回答什么

本篇是窗口与会话两侧的语义采购。回答四组问题：ICCCM 与 EWMH 两份约定到底定了哪些窗口语义（类型、状态、焦点、工作区、装饰协商），焦点两派的取舍与键盘焦点、指针焦点的分离，窗口操作全集的语义边界，以及 Wayland 协议族五件、显示管理器加 logind 加会话恢复这条登录链。收尾对照 vesper 的单色单进程起点，给必做清单、负面清单与 D1、D2 两刀的边界建议。

一个贯穿全篇的观察先摆出来。ICCCM 与 EWMH 是客户端与窗口管理器之间的纸面约定，全部住在用户态。X server 只提供重定向与叠放这类原语，内核对「窗口」一无所知，窗口管理是用户态的政策，这两份文档本身就是证据。反方向，logind 与 seat 是桌面里真正长在操作系统上的那一小段：设备归属、会话边界、VT 管辖。vesper 的三梯队恰好横跨两端。

方法论也定在这里。vesper 单进程起步，WM 与「客户端」住在同一进程，ICCCM 与 EWMH 的线格式（atom、ClientMessage）对 vesper 没有意义，但它们整理出的语义清单是窗口件数据结构的一手设计参照。借语义，不搬线格式。

## 1. ICCCM：窗口管理语义的底册

ICCCM（Inter-Client Communication Conventions Manual）版本 2.0 于 1994 年 4 月定稿，X Consortium 标准，随 X11R6 发布。版本史很短：第一版由 Sun Microsystems 的 David Rosenthal 编写，第二版由 SunSoft 的 Stuart W. Marks 修订（来源为 x.org 文档的 tronche 镜像前言，x.org 官方路径当前重定向到 archive）。它管客户端与 WM 之间怎么打交道：WM 本身是一个普通 X 客户端，通过重定向 root window 的 MapRequest 与 ConfigureRequest 接管窗口的映射与配置，EWMH 第 9.7 节明说这些事件的处理「以 ICCCM 为准」。

四块核心语义如下。

第一块，关窗协议。客户端在 WM_PROTOCOLS 属性里报名参加哪些协议，ICCCM 定义三个原子：WM_TAKE_FOCUS（输入焦点让渡）、WM_DELETE_WINDOW（请求删除顶层窗口）、WM_SAVE_YOURSELF（保存状态，已标 deprecated）。关窗因此有两档。客户端报名了 WM_DELETE_WINDOW 时，WM 发一条 ClientMessage，等客户端自己决定（可能弹确认框）后撤窗，规范原话是 WM 不应对这种窗口使用 DestroyWindow。没报名的，用户请求删除时客户端「可能被 server 断开」，EWMH 第 4.1 节把这一档的常用实现明写为 XKillClient。礼貌与武力分开，1994 年就定下了。

第二块，状态机。WM_STATE 三个值：WithdrawnState（未映射不受管）、NormalState、IconicState。客户端想撤窗，自己 unmap 并补发一个合成的 UnmapNotify 给 root，理由原文是「确保 WM 确实收到客户端想改状态的信号」。客户端想最小化，发 WM_CHANGE_STATE 消息带 IconicState。映射时的初始状态由 WM_HINTS 的 initial_state 给出。

第三块，尺寸提示。WM_NORMAL_HINTS 一个结构装下 min 与 max 尺寸、base 尺寸、宽高增量（width_inc、height_inc）、纵横比、窗口重力 win_gravity。首选尺寸的公式是 width = base_width + i × width_inc，height 同理。xterm 这类网格程序报 cell 尺寸，WM 就能把用户的拖拽对齐到字符格。旗标区分 US（用户指定）与 P（程序指定），USPosition 表示位置是用户摆的、WM 应尊重。win_gravity 说明加框时客户端窗口怎么让位（StaticGravity 表示内部不动）。固定尺寸窗口的表示法是 min 等于 max，EWMH 9.5 重申了这条。

第四块，临时窗与 override-redirect。WM_TRANSIENT_FOR 指向另一个顶层窗口，含义是本窗「替那个窗口弹出」，WM 可以免装饰并直接映射。菜单类瞬时窗走另一条路：override-redirect。传统 WM 完全忽略它们（EWMH 2.11 原话），合成器时代才被合成管理器接管。

装饰本身 ICCCM 几乎不管，只管几何让位。装饰协商的历史答案先是 Motif 的 _MOTIF_WM_HINTS，EWMH 对它的批评是「纯视觉层面的装饰描述」，而 WINDOW_TYPE「通过描述窗口的功能，让 WM 对同型窗口应用一致的装饰与行为」。这是 EWMH 用类型取代装饰描述的理由原文。

### 1.1 焦点语义：四模型

ICCCM 4.1.7 把焦点接受方式做成两个正交位的表：WM_HINTS 的 input 旗标（WM 可不可以把焦点设给这个窗），与是否报名 WM_TAKE_FOCUS（要不要用消息通知而不是被直接设置）。

| input 旗标 | WM_TAKE_FOCUS | 名称 | 行为 |
|---|---|---|---|
| 否 | 无 | No Input | 从不接受键盘输入，例如 xload |
| 是 | 无 | Passive | 接受焦点，从不自己设置 |
| 是 | 有 | Locally Active | 自己设置焦点，但只在自己已有焦点时 |
| 否 | 有 | Globally Active | 拒绝 WM 设焦点，自己从任意位置抓取 |

焦点转移的流程：WM 发 ClientMessage（WM_TAKE_FOCUS 带时间戳），客户端若要焦点，回复 SetInputFocus 到自己「上次有焦点的窗口或默认输入窗口」，时间戳用消息里带的（永不用 CurrentTime），revert-to 设为 Parent。客户端也可以拒绝，规范举的例子是「时钟，或一个没开任何编辑框的文本编辑器」。焦点在这里是所有权让渡，WM 问，客户端答。

## 2. EWMH：桌面时代的扩充

EWMH（Extended Window Manager Hints）版本 1.5，发布日 2011-11-29，更新 2013-01-25，出处 freedesktop.org。它的出身写在引言里：GNOME 与 KDE 当年各自给 ICCCM 打私有扩展，EWMH 把这些扩展标准化成任何桌面都能采用的一组补充，并让 pager 与 taskbar 可以做成独立于 WM 的客户端。采用前提是先守 ICCCM。

### 2.1 窗口类型

_NET_WM_WINDOW_TYPE 在映射前由客户端设置，WM 用它决定装饰、叠放位置与行为。共十四型：

| 类型 | 用途 | 备注 |
|---|---|---|
| DESKTOP | 桌面面（壁纸与图标所在的那块） | 全屏一块，EWMH 9.2 建议贴所有桌面 |
| DOCK | 面板与 dock | WM 通常保持置顶 |
| TOOLBAR、MENU | 从主窗撕下的工具条与菜单 | managed 窗口用 |
| UTILITY | 常驻小工具窗（调色板一类） | 区别于 TOOLBAR 与 DIALOG |
| SPLASH | 启动画面 | |
| DIALOG | 对话框 | 未设类型但有 WM_TRANSIENT_FOR 的按此处理 |
| DROPDOWN_MENU、POPUP_MENU、TOOLTIP、NOTIFICATION、COMBO、DND | 瞬时窗 | 通常用 override-redirect，类型主要供合成器统一处理 |
| NORMAL | 普通顶层窗 | 缺省值 |

类型按偏好排序报，必须含至少一个基本型，未知的扩展型因此有缺省行为。

### 2.2 状态提示

_NET_WM_STATE 是原子列表，在列即置位。十三态：MODAL、STICKY、MAXIMIZED_VERT、MAXIMIZED_HORZ、SHADED、SKIP_TASKBAR、SKIP_PAGER、HIDDEN、FULLSCREEN、ABOVE、BELOW、DEMANDS_ATTENTION、FOCUSED。职责划分严格：WM 必须持续把此属性更新为当前事实，客户端要改只能发 _NET_WM_STATE 消息（ADD、REMOVE、TOGGLE 三种动作，可一次带两个属性以支持横竖同时最大化），WM 决定改不改。HIDDEN 专门由 WM 设置，最小化是它的规范例子，实现注记还写了客户端请求 toggle HIDDEN 时，WM 应当无视，因为 HIDDEN 是其它状态的函数，非独立态。ABOVE 与 BELOW 规范明说主要供用户偏好，应用想引起注意应走 urgency 旗标。

### 2.3 工作区

模型是「一到多个完全独立的桌面，每个可以比屏幕大」（EWMH 9.1）。桌面大于屏幕时屏幕是 viewport，_NET_DESKTOP_VIEWPORT 记每个桌面的视口位置。_NET_WM_DESKTOP 记窗口在哪桌，0xFFFFFFFF 表示出现在所有桌面。注意 STICKY 与桌面无关，它管的是 viewport 滚动时窗口保持在屏幕原位，容易被望文生义，EWMH 在 _NET_WM_ACTION_STICK 的注记里专门强调这点。虚拟桌面的实现注记给了无虚拟根的方案：非当前桌面的框直接 unmap（ICCCM 语境下进 IconicState），配 HIDDEN 让 pager 区分「在别的桌面」与「最小化」。

_NET_WORKAREA 给每桌一个扣除 dock 与面板后的工作区，扣除量来自 _NET_WM_STRUT_PARTIAL（四边保留量加各自起止坐标，共 12 值）。任务栏与面板靠它声明「这块别盖我」，最大化对齐它，桌面图标也按它摆。

### 2.4 激活、来源与防抢

EWMH 2.8 定义激活：给窗口输入焦点，可能还要搬桌面、还原、抬升。客户端想激活别的窗口要发 _NET_ACTIVE_WINDOW，带来源标识：1 是应用自发，2 是 pager 等直接用户动作的代表，0 是旧版客户端。WM 可以拒绝应用来源的请求，原话给了两种处置（完全忽略，或改标 DEMANDS_ATTENTION）。配套的 _NET_WM_USER_TIME 让 WM 区分「用户刚点的」与「定时器弹的」，后者不该偷焦点。

### 2.5 动作许可

_NET_WM_ALLOWED_ACTIONS 由 WM 维护，十二个动作原子（MOVE、RESIZE、MINIMIZE、SHADE、STICK、MAXIMIZE_HORZ、MAXIMIZE_VERT、FULLSCREEN、CHANGE_DESKTOP、CLOSE、ABOVE、BELOW），在列即许可。它与状态是两张表：状态是「现在怎样」（WM 写的事实），许可是「用户能对它做什么」（WM 写的政策），任务栏与 pager 读它决定给用户亮哪些操作。EWMH 还提醒 WM 初管一个窗口时应无视此属性的残留值，那可能是上一个 WM 留下的政策。

## 3. 焦点两派与键盘、指针焦点的分离

click-to-focus（CTF）是点击定焦点，指针位置不影响焦点。focus-follows-mouse（FFM）是焦点跟随指针进入，i3 用户手册的默认行为原文是「By default, window focus follows your mouse movements as the mouse crosses window borders」，可用 focus_follows_mouse no 关掉。sloppy focus 是温和版：指针离开窗口到桌面时不交还焦点（术语出自 fvwm 传统，此处为通识判断，未附单一出处 URL）。

FFM 有一个对账问题，同一本 i3 手册里就有现成证据：键盘焦点切到另一输出的窗口时，i3 默认把指针 warp 过去（mouse_warping），否则「焦点在 A 窗、指针在 B 窗」成为常态，点击会落在非焦点窗口上。

键盘焦点与指针焦点是两件事，X 从根上就这么切：键盘焦点是全局单值（SetInputFocus，ICCCM 讨论的就是它的让渡），指针事件的去向由命中位置决定，两者可以分叉。ICCCM 的四模型是把键盘焦点让渡协议化，EWMH 的 USER_TIME 与来源标识是把「谁有权改焦点」分级。Wayland 侧同构：键盘焦点由合成器的 enter 与 leave 决定，指针命中独立分派（此句为协议常识，未附单一出处 URL）。

取舍给 vesper 用一句话说清。FFM 省一次点击，代价是拖选、悬停菜单、滚轮都容易把焦点带到不想去的地方，并且要求焦点策略理解指针全轨迹。CTF 把焦点转移变成显式事件，与捕获语义（按下者持有）天然咬合。桌面环境主流取 CTF（此处为通识判断），平铺 WM 常默认 FFM（i3 手册为证）。

## 4. 窗口操作语义表

八个操作逐个给语义与边界，出处列在表内。

| 操作 | 语义 | 出处与边界情况 |
|---|---|---|
| 移动 | 改框位置，WM 全权，无协议商量 | EWMH 的 _NET_WM_MOVERESIZE 让客户端自绘把手、由 WM 执行操作，获得统一样式。支持键盘移动与 CANCEL。竞态有明文：客户端发消息时用户可能已松键，WM 应另给终止手段（ESC）。xdg-shell 侧 compositor 可在 maximized 时拒绝 move |
| 缩放 | 尺寸变化受 WM_NORMAL_HINTS 约束 | base 加 inc 公式对齐字符格，min 等于 max 即禁缩放，gravity 定哪个角不动。净拖拽缩放有八个方向枚举。终端的 resize 联动见 terminal-emulator-internals.md（TIOCSWINSZ 与 SIGWINCH） |
| 抬升 | 把窗口移到兄弟序列上方 | 分层序见第 5 节。EWMH 明说 WM 可对应用的自行抬升设限，_NET_RESTACK_WINDOW 只该 pager 用，pager 的请求应总是被服从 |
| 压低 | 对称操作，受层约束 | 分层序是推荐不是强制，普通窗口能否压到 desktop 层以下由 WM 自定 |
| 最小化 | Normal 转 Iconic，窗口 unmap | 客户端侧发 WM_CHANGE_STATE。EWMH 补 HIDDEN 给 pager 判断显示，并断言任务栏环境下图标常被认为不合适，任务清单接管了「我开着什么」的问答 |
| 最大化 | Normal 的子状态，非 ICCCM 正式态 | EWMH 2.1.1：早期 ICCCM 草案有过 ZoomedState 后删除。目标对齐 workarea 而非全屏，WM 应记住原几何以便还原，横竖可独立，多显示器后语义更复杂 |
| 关闭 | 两档：DELETE 协议或断开连接 | 见第 1 节。EWMH 的 _NET_CLOSE_WINDOW 让 pager 请 WM 代办，理由是 WM 可能更聪明（例如加超时）。配套 _NET_WM_PING 探活，无响应可凭 _NET_WM_PID 加 WM_CLIENT_MACHINE 杀进程 |
| 还原 | 两义：从 Iconic 回 Normal，或从最大化回记忆几何 | EWMH 明说最大化与全屏的还原几何由 WM 负责。从 Iconic 还原会触发激活的组合动作 |

表的读法：每个操作的协议面只有两种形状，要么是客户端对 WM 的请求（WM_CHANGE_STATE 与 _NET_* 消息），要么是 WM 对客户端的通知（ClientMessage 与合成 unmap 事件）。几何本身永远由 WM 落定，客户端只能请求与被告知。

## 5. z 序、工作区与合成器的分合

z 序在窗口树内就是兄弟序。EWMH 给两份清单：_NET_CLIENT_LIST 按初始映射序（最老在前），_NET_CLIENT_LIST_STACKING 按自底向上叠放序。任务栏吃前者（条目稳定），任务切换器吃后者，Windows 95 的 Alt+Tab 按 Z 序排图标（lightweight-and-classic-desktops.md §5.1）。叠放不只是一根线性栈，EWMH 9.10 推荐分层，自底向上：

1. WINDOW_TYPE_DESKTOP 的窗口
2. _NET_WM_STATE_BELOW 的窗口
3. 不属于其它层的普通窗口
4. WINDOW_TYPE_DOCK 与 _NET_WM_STATE_ABOVE 的窗口
5. 有焦点的 FULLSCREEN 窗口

transient 应保持在其宿主之上。同一层内才是用户 raise 与 lower 的自由区。

合成器在 X11 下是第三个角色。EWMH 第 8 章专门给 compositing manager 立规矩：它是一个 X 客户端，用 Composite 扩展把所有窗口重定向到离屏 pixmap，用 Damage 扩展跟踪重绘，负责把 pixmap 画回屏幕（可加半透明与变形）。每个屏要取得 _NET_WM_CM_Sn 的 manager selection，用的又是 ICCCM 2.8 的 selection 机制，与 WM 本体的 WM_Sn 平行。历史线（Wikipedia 的 Compositing window manager 条目）：Metacity 2.8.4（2004-08）带来早期合成，Compiz（2005-01-26）把全加速 3D 合成带上 Linux，Xfwm（2005-01）是第一个被广泛宣传的合成 WM，KWin 自 4.0 起合成。独立合成器（xcompmgr、picom）可以配任何 WM 用。传统 WM 忽略的 override-redirect 窗口，合成器必须一起画（EWMH 2.11），这是菜单阴影一类效果的基础。

Wayland 把三个角色合一。weston 手册页的原话是它「a display server, a window manager, and a compositor all in one」（经 construction-order.md §5 转引，原页 man.archlinux.org/man/weston.1.en）。客户端拿不到全局坐标，位置由合成器裁定（xdg-shell 明文），WM 与合成器不再需要 EWMH 这样的中介协议，桌面语义内化成合成器的实现细节。副作用是，本来 EWMH 一份文档管完的题目（层、装饰、选区）各自长成独立小协议，见下一节。

结构差异的教学价值：X11 是 server、WM、合成器三角色三组约定的缝合体，Wayland 是单角色单实现。vesper 单进程起步的形状天然是 Wayland 形状：树序即 z 序，脏区条带重画即合成，没有跨进程协商。D5b 上真 Wayland 后端时宿主表三口不变，变的只是 vesper 作为一个客户端怎么把整张画布递出去。

## 6. Wayland 协议族五件

### 6.1 xdg-shell

现行第 7 版，属 wayland-protocols 的 stable 类目（版本特性沿革：v2 加 tiled 状态，v3 加 positioner 重定位，v4 加 configure_bounds，v5 加 wm_capabilities，v6 加 suspended，v7 加 constrained 边）。角色协议：wl_surface 经 xdg_wm_base 取得 toplevel（窗）或 popup（菜单与提示）角色，先 configure 后 ack 的握手驱动几何，客户端不设绝对位置。

toplevel 给足窗口语义：configure 事件带状态集（maximized、fullscreen、resizing、activated、tiled_*、suspended），set_app_id 的取值约定是 .desktop 文件基名，这与 gap-analysis 1.10 里 App 注册表半取 desktop entry 字段语义的拍板正好对上。交互式移动缩放走 request_move 与 request_resize，要求响应真实用户事件（带 serial），合成器可拒绝。popup 用 positioner 定位，anchor 加 gravity，约束调整按 flip、slide、resize 的优先级（原文顺序）保证弹出物不越屏。grab 要求父必须是 toplevel 或正抓取的 popup，只有最顶 popup 可销毁，dismiss 时发 popup_done。子面必须与父面相交或至少相邻，菜单不得飘走。

### 6.2 layer-shell（wlr-layer-shell-unstable-v1）

不在 wayland-protocols，是 wlroots 生态的 wlr 扩展，面板、壁纸、锁屏、通知靠它。四层自底向上是 background、bottom、top、overlay，普通 shell 面画在 bottom 与 top 之间，全屏面通常在 top。定位用 anchor 边加 margin。独占区是任务栏的核心机制，文档的例子是面板把 exclusive zone 设为 10，最大化的 shell 面就不会压在它上面，这是 struts 的直系后代。keyboard interactivity 三档：none（永不取键盘焦点，桌面挂件用）、exclusive（顶层独占，锁屏用）、on_demand（v4 起，按常规焦点语义可被用户聚焦，面板用）。支持面按 wayland.app 页面：KWin、Sway、Labwc、Hyprland、river、niri、Wayfire、Mir、COSMIC 等支持，Mutter、Weston、Cage 不支持，GNOME 系与 wlroots 系在面板协议上至今分家。

### 6.3 primary-selection（unstable-v1）

给 Wayland 补 X11 PRIMARY 的同构物：选即有，中键粘贴，目的原文是与 X server 的 primary selection 对应。所有权由合成器跟踪，数据留在源端，经管道 fd 传输，MIME 类型协商，set_selection 要带触发按下的 serial。已裁定的 D4 终端方案（拖选写 PRIMARY、中键粘贴，见本次调研背景的既有拍板）是同一条语义的 Wayland 版，desktop-infrastructure.md §6 的剪贴板裁定在这里得到协议级印证。

### 6.4 xdg-decoration（unstable-v1）

装饰归谁画的谈判协议。诞生背景是早期合成器政策分裂，有的坚持客户端自绘，有的要服务端画。客户端 set_mode 只是偏好，最终由 configure 事件裁定，客户端必须照办并避免谈判循环。支持面同样分裂：KWin 支持，Mutter 与 Weston 不支持。对 vesper 的参照价值在概念：装饰权是桌面壳的属性。vesper 的 WindowFrame 永远由桌面画，等于永远 server-side，设计文档里写死即可，不需要谈判协议。

### 6.5 input-method（unstable-v2）

合成器侧的输入法挂口：每 seat 至多一个输入法对象，activate 与 deactivate 切换，合成器把 surrounding text、content type、光标矩形递给输入法，输入法回 preedit、commit、删 surrounding。grab_keyboard 给输入法独占键盘，交付原始键码加 keymap，keysym 自己译，CJK 组词必需。它是 text-input 的另一半：应用到合成器走 text-input-v3，合成器到输入法走本协议，正好对应 desktop-infrastructure.md §7 所记 KWin 用 v1、wlroots 系用 v2 的分野，wayland.app 的支持矩阵（Sway、Labwc、river、niri、Hyprland、COSMIC 在，KWin、Mutter、Weston 不在 v2）与此互相印证。D5b 依赖清单里 text-input-v3 已在册，本协议是它的对端。

五件的公共形状值得记录：EWMH 一份文档里的窗口类型、层、struts、装饰四个题目，在 Wayland 各自变成独立小协议，谈判语义统一收敛为「客户端表达偏好，合成器 configure 裁定」。D6 的跨进程协议设计直接可抄这个形状。

## 7. 会话层：显示管理器、logind 与 seat、恢复的最小形状

### 7.1 显示管理器

xdm 是起点：X11R3（1988-10）引入，Keith Packard 写的（Wikipedia 的 X display manager 条目）。职责是图形化的 getty 加 login：本地先起 X server 再画登录面，远端方向反过来，XDMCP（X Display Manager Control Protocol，X11R4 于 1989-12 加入，UDP 177）让 X server 主动向主机请求一个显示管理器连接，认证明文，同页警告它与 telnet 同级。现代家族 GDM、SDDM、LightDM 增加的是主题与 greeter（登录界面本身是一个会话），基本盘没变。

### 7.2 logind 与 seat

权威定义在 sd-login(3) 手册（Arch 镜像，概念条目自 systemd 235 加入）。seat 的定义原文：「A seat consists of all hardware devices assigned to a specific workplace」，至少一块显卡，通常带键盘鼠标，名字形如 seat0。session 的定义：从用户登录到登出的整段时间，绑定零或一个 seat，同 seat 多会话只有一个 active。设备归属由 udev 标签管：seat 标签决定资格，master-of-seat（显卡的 framebuffer）是 seat 成立的充分条件，uaccess 把设备访问权跟「该 seat 上有 active 会话」绑定。VT 只在 seat0 存在，其它 seat 无内核 VT（systemd.io 的 Writing Display Managers）。

显示管理器与 logind 的接缝（同文）：DM 经 PAM 会话栈（pam_systemd）注册会话，greeter 会话标 XDG_SESSION_CLASS=greeter 以便应用过滤，X server 由 DM 用 -seat 参数告知 seat。多 seat 时 DM 订阅 SeatAdded 与 SeatRemoved，只在 CanGraphical 为真的 seat 上起 greeter。这段链条的教学价值在「图形登录的每一环都是 OS 服务」：PAM、udev、VT、cgroup，没有一环是 GUI。

### 7.3 会话恢复的最小形状

XSMP（X Session Management Protocol）是恢复的全量协议，跑在 ICE 上，会话管理器的地址放 SESSION_MANAGER 环境变量（xorg.freedesktop.org 的 libSM 文档）。要点：客户端 RegisterClient 时可携带上一会话的 previous-ID，ID 全局唯一（版本号加地址加时间戳加 PID 加序号构造），凭它跨会话保持身份。保存循环是 SaveYourself（type 分 Global、Local、Both，带 shutdown 旗，interact 机制让客户端排队与用户对话，fast 旗对应掉电抢救），管理别人的客户端（典型是 WM）可申请 phase 2，等大家安静后再存。属性表里 RestartCommand 是重启命令（应内嵌 client-ID），DiscardCommand 丢弃存量状态，RestartStyleHint 四档：RestartIfRunning（默认）、RestartAnyway（退了下次也拉起）、RestartImmediately（当场拉起）、RestartNever。

ICCCM 第 5 节给 WM 的定位值得单独看：WM 自己也是会话客户端（要注册拿自己的 client-ID），它要为每个带有效 SM_CLIENT_ID 的客户端窗口保存与恢复状态，并在 checkpoint 的 phase 2 做。窗口归属用 WM_CLIENT_LEADER（客户端的领导窗）加 WM_WINDOW_ROLE 区分同客户端的多个窗。老式的 WM_COMMAND 重启约定在 ICCCM 2.0 已挪进附录 C 标为 obsolete。

对照与取舍。CDE 工作区的占用列表（窗口属于哪些工作区是一个可枚举属性，lightweight-and-classic-desktops.md §5.3 引 Oracle 文档）与 XSMP 代表恢复问题的两端：前者是 WM 自己就能存的廉价事实（几何、桌面占用、还原命令的快照），后者是任意客户端自愿参与的协议机器（双向消息、两相、interact、身份、认证）。恢复的最小形状取前者：一份快照加一次回放。单进程期快照就是窗口树加 App 注册表的序列化，D6 之后子程序在会话里的状态才需要「请子程序自存自报」，那时的参照是 XSMP 的 previous-ID 身份加自存属性，不需要 ICE 级的传输与谈判机器。

## 8. 对 vesper：必做、显式不做与两刀边界

### 8.1 必做（单色单进程期就要立住的语义）

- 键盘焦点一个变量，click-to-focus，来源字段从第一天就有。理由：与 InputRouter 捕获语义咬合（按下者持有，焦点转移与事件捕获同源），拖选不抢焦点（终端 PRIMARY 裁定的前置），FFM 的指针全轨迹理解成本换不来收益。来源字段抄 EWMH source indication 的思想，单进程期只有「用户动作」一种来源，字段留着，D6 后「子程序请求」是第二种。
- z 序即树序，抬升策略拍死。PointerDown 落在标题栏或客户区，抬升加取焦点一次完成，activation 的原子性照 EWMH 2.8 的组合动作清单验收：焦点、还原、搬桌面三件事要么全做要么不做。
- 窗口类型位图进 App 报名数据。tooltip、dropdown、菜单类永不取焦点，从第一天就是规则而非巧合。层序照 EWMH 9.10 摆好：桌面面最底，普通窗中层，任务栏与浮层在顶。
- 状态与许可两张表分离。窗口对象存事实（maximized、iconic）与许可（可移动、可缩放、可关闭）各一套，任务栏读许可表决定亮什么。EWMH 把它们拆成两个原子的理由对单进程同样成立：事实会自己变，许可是政策。
- 关闭两档。on_close 生命周期回调（请示档）与强制销毁（武力档）分开，D4 的 PTY 子进程收 SIGHUP 就是武力档的第一个真实消费者。_NET_WM_PING 的同构物记进 D6 口袋：子程序探活。
- 尺寸提示进 WindowFrame。min 第一天就有（零尺寸窗口是崩溃不是边界），base 加 inc 等 resize 刀（gap-analysis 1.6 建议贴 D4）补齐，终端网格 80 列乘 cell 宽就是天然的 base 加 inc。固定尺寸窗口 min 等于 max 直接可用。
- 最大化记忆几何与 workarea 概念。pre-maximize bounds 存两个字段，最大化目标对齐扣掉任务栏后的区域。任务栏刀（construction-order.md 修正一）与这条咬合：任务栏先立，workarea 才有内容。

### 8.2 显式不做（写进文档的负面清单）

- ICCCM 与 EWMH 的线格式。X11 后端上 vesper 自己只是一个普通全屏客户端（visor 的 X11WindowHost），不 reparent、不管别人的窗。两份规格降格为数据结构设计参照，教学叙事就写「协议折叠为函数调用」。
- 多工作区。gap-analysis 1.11 已推迟，词汇表记两条：占用列表模型（0xFFFFFFFF 语义）与 STICKY 的真义（管 viewport 不管桌面）。
- focus-follows-mouse 与 sloppy。不做，但键盘焦点与指针命中在代码里保持两个独立事实（焦点变量与 hit_test 结果不共享），将来加 FFM 是策略件不是结构返工。layer-shell 的 keyboard interactivity 三档是「每层对键盘的态度」的现成抽象：浮层永远 none，桌面面 on_demand，普通窗独占。
- 合成器分离。1bpp 无透明无变形，树序绘制就是全部合成。X11 三角色缝合的历史留给教学叙事，不进代码。
- 会话管理器与图形登录。会话等价物是启动装配（desktop-infrastructure.md §2 拍板），登录推迟到 Cinux 侧 init 线程阶段。seat 与 VT 的知识记进 D5 口袋：initramfs 里 /init 拉桌面就是 seat0 上唯一的会话，evdev 节点在无 logind 的世界里就是直接读设备，概念上知道 uaccess 的形状（活动会话才有设备权）即可。

### 8.3 D1（WindowFrame 拖拽）刀面边界

- 抬升与取焦点的原子性落在这一刀，判例建议：拖动开始时断言窗口已置顶且已取焦点。
- 最小尺寸钳制同刀带上，min 先取硬编码值（比如 2×2 字符网格），resize 刀再换成 App 报数。
- 拖出画布的钳制策略规格无条文（本文拉取的 ICCCM 与 EWMH 章节均未规定），自己拍死：标题栏保持至少部分可达，写进判例与 CLAUDE.md。
- 拖拽期间的事件归属用 InputRouter 捕获语义（已有），新旧两框报脏（交接文档 D1 已定），这两条不动。

### 8.4 D2（桌面图标）刀面边界

- 桌面面不是窗口。它是容器层最底部的兄弟，等价 EWMH 的 WINDOW_TYPE_DESKTOP 加 9.2 判例：一个 desktop 型全屏面「使桌面可聚焦并极大简化实现」，比管理一堆小形状窗快。vesper 里它不进 WindowFrame 序列、不参与 z 序操作、不可最小化。
- 单击选中不等于激活。选中态是视觉事实，不夺键盘焦点（desktop 层键盘态度 on_demand）。双击才是激活，激活等于开窗加抬升加取焦点的组合，判例写纯逻辑案（双击开窗，交接文档 D2 已定）。
- 图标不是窗口的另一层含义：D6b 图标拉起子程序时，图标数据来自 App 注册表（desktop entry 字段语义），与窗口管理无关。

### 8.5 教学论据的投影

窗口管理全在用户态约定里，ICCCM 加 EWMH 两份文档就是证据本体：X server 只出了重定向与叠放原语，内核什么都没出。vesper 单进程把协议折叠成函数调用，是这条论据走到极致的形状。反方向，会话层的底座（PAM、udev、VT、cgroup）全是操作系统件。「GUI 离操作系统很远」与「桌面的登录底座离操作系统很近」是同一枚硬币的两面，正好支撑三梯队叙事：X11 梯队看不见 seat，fbdev 梯队踩在 seat0 的 VT 上，Cinux 梯队自己就是 init。

## 9. 对 vesper 的启示

- 窗口类型位图进 App 报名数据，浮层类永不取焦点写成语义规则。（概念参考 EWMH 的 WINDOW_TYPE 类型学）
- 状态与许可两张表分家，任务栏按许可表渲染操作。（概念参考 EWMH 的 state 与 allowed actions 分离）
- 关闭分请示与武力两档，on_close 回调对位 WM_DELETE_WINDOW，SIGHUP 对位断连。（概念参考 ICCCM 的 WM_DELETE_WINDOW 两档关闭）
- 焦点是显式让渡，来源字段（用户动作、子程序请求）进接口。（概念参考 ICCCM 的 WM_TAKE_FOCUS 让渡模型，概念参考 EWMH 的 source indication）
- 终端窗口的尺寸语义用 base 加 inc 公式，min 等于 max 表固定尺寸。（概念参考 ICCCM 的 WM_NORMAL_HINTS 尺寸公式）
- 桌面面、普通窗、任务栏与浮层的层序第一天摆死，raise 与 lower 的自由区只在同层内。（概念参考 EWMH 的分层叠放序）
- 最大化的目标区域是 workarea 不是整屏，任务栏占条之后 workarea 才有定义。（概念参考 EWMH 的 struts 与 workarea）
- 子程序自绘把手、桌面执行移动缩放，交互统一样式，竞态给 ESC 终止。（概念参考 EWMH 的 _NET_WM_MOVERESIZE 与 xdg-shell 的 request_move）
- 会话恢复先做快照回放，子程序参与时用身份加自存自报。（概念参考 XSMP 的 previous-ID 会话身份）
- D6 协议的谈判形状统一取「客户端表达偏好、桌面裁定」，configure 而非 command。（概念参考 xdg-shell 的 configure 握手）

## 10. 参考来源

规格与一手文档：

- https://tronche.com/gui/x/icccm/ 、https://tronche.com/gui/x/icccm/sec-4.html 、https://tronche.com/gui/x/icccm/sec-5.html （ICCCM 2.0 镜像。版本史与日期、WM_PROTOCOLS 三原子、四焦点模型与 WM_TAKE_FOCUS 流程、WM_STATE 三值与合成 UnmapNotify、WM_NORMAL_HINTS 字段与公式、WM_TRANSIENT_FOR、客户端领导窗与 phase 2。支撑第 1、3、4、7.3 节。x.org 官方路径 www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html 当前 301 重定向至 archive，本文取镜像）
- https://specifications.freedesktop.org/wm-spec/latest/ 与分章 https://specifications.freedesktop.org/wm/latest/ 的 ar01s02、ar01s03、ar01s04、ar01s05、ar01s06、ar01s08、ar01s09 各页 （EWMH 1.5。版本与日期、非 ICCCM 特性（最大化史、虚拟桌面实现注记、任务栏、激活定义）、根窗属性（desktop 族、workarea、struts、active window）、应用属性（十四型、十三态、allowed actions、USER_TIME）、WM 协议（PING、MOVERESIZE 族、RESTACK）、合成管理器章、实现注记（分层序、来源标识、file manager desktop 判例）。支撑第 2、3、4、5 节）
- https://wayland.app/protocols/xdg-shell （xdg-shell 第 7 版。角色与握手、toplevel 与 popup 语义、positioner 优先级、app_id 约定。支撑第 6.1 节）
- https://wayland.app/protocols/wlr-layer-shell-unstable-v1 （四层、anchor 加 margin、exclusive zone、keyboard interactivity 三档、支持矩阵。支撑第 6.2 节）
- https://wayland.app/protocols/primary-selection-unstable-v1 （选即有与中键粘模型、所有权与 serial、管道传输。支撑第 6.3 节）
- https://wayland.app/protocols/xdg-decoration-unstable-v1 （装饰谈判与 configure 裁定、支持面。支撑第 6.4 节）
- https://wayland.app/protocols/input-method-unstable-v2 （activate 循环、grab_keyboard、与 text-input 的两段链、支持矩阵。支撑第 6.5 节）
- https://xorg.freedesktop.org/releases/X11R7.7/doc/libSM/xsmp.html （XSMP。消息集、属性表、RestartStyleHint 四档、previous-ID 构造。支撑第 7.3 节）
- https://man.archlinux.org/man/sd-login.3.en （seat 与 session 定义原文、udev 标签族、seat0。支撑第 7.2 节）
- https://systemd.io/WRITING_DISPLAY_MANAGERS/ （greeter 会话类、-seat 参数、CanGraphical、VT 只在 seat0。支撑第 7.2 节）

二手与转引：

- https://en.wikipedia.org/wiki/X_display_manager_(program_type) （xdm 的 X11R3 1988 出处与作者、XDMCP 的 X11R4 与 UDP 177、现代 DM 家族。支撑第 7.1 节）
- https://en.wikipedia.org/wiki/Compositing_window_manager （合成 WM 定义、Metacity 2.8.4 与 Compiz 与 Xfwm 时间线、独立合成器、Wayland 合一表述。支撑第 5 节）
- https://i3wm.org/docs/userguide.html （focus_follows_mouse 默认原文、mouse_warping。支撑第 3 节）
- https://man.archlinux.org/man/weston.1.en （合成器三合一原话，经 construction-order.md 转引后本文复引。支撑第 5 节）

本地材料：

- 本仓 pack-26-desktop-handover.md（2026-10-20 起瘦身并入 documents/desktop-roadmap.md；D1 至 D6 刀面、宿主表现状、D5b Wayland 边界。支撑第 0、8 节）
- documents/research/lightweight-and-classic-desktops.md （CDE 工作区占用列表、Windows 95 任务栏与 Alt+Tab 的 Z 序论据。支撑第 2.3、5、7.3 节）
- documents/research/vesper-gap-analysis.md （1.3 焦点域、1.5 Z 序、1.6 resize、1.10 注册表、1.11 工作区、1.12 会话的既有拍板。支撑第 6.1、8 节）
- documents/research/desktop-infrastructure.md （第 2 节会话拍板、第 6 节剪贴板裁定、第 7 节输入法两段链。支撑第 6.3、6.5、8.2 节）
- documents/research/construction-order.md （weston 三合一转引、修正一任务栏刀。支撑第 5、8.1 节）
- documents/research/terminal-emulator-internals.md （resize 的 TIOCSWINSZ 与 SIGWINCH 链路。支撑第 4 节）
