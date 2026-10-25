# 桌面路线：终局形态与 D5b/D6 切法

> 自 pack-26-desktop-handover.md（2026-10-17 交接文档）瘦身而来，2026-10-20。原文的仓骨架盘点、visor 现状快照、D0-D5 已完成刀法与已拍板的岔口不再保留，这里只放还活着的路线。D0-D5 的现状见 README，通道运维见 fbdev-vm-lane.md。

## 终局形态（2026-10-17 拍板，未变）

- 桌面不只是壳，是程序启动器加窗口宿主：桌面上摆图标，图标拉起真二进制程序，每个程序一个窗口。Linux 侧 posix_spawn 拉本机程序，Cinux 侧将来 fork/exec 拉 Cinux 用户程序。这条线对应旧 `HostDesktop::spawn` 扩展口，与 visor stage-4「宿主表要不要第四口 spawn」联动，届时两边一起拍。
- 进程形态：桌面本体（窗口树、摆放、合成）单进程，但「窗口内容向桌面递像素、桌面向窗口递事件」这条边从 D0 起就以进程内接口的形状存在（递数据、不共享状态）。D6 换真进程时桌面结构不推倒重来，跨进程协议推迟到 D6 定稿。
- 产品形态：「桌面本体 + 一组 App」，完成度向 Windows 看齐但不抄设计。第一版身份原为 Mono1 黑白加圆角，2026-10-23 彩色化升级为暮金加暖纸的彩色面（七槽主题、XRGB8888 画布），圆角不变。App 从第一天对着一个 App 口写（报名字、给图标、造窗口内容、收生命周期回调），单进程期是编译进来的注册模块，D6 后同一接口换真进程。
- 生态三级：终端即命令行生态宿主（PTY 加 VT 是开放标准）；自有 App 口定位教学通道加 Cinux 通道；第三级 Wayland 合成器于 2026-10-24 由「远期」提为主线（作者拍板：X11 与 Wayland 应用上桌，走根治路线，见 C 系列），X11 应用经 XWayland 接待、不做 XReparentWindow 过渡路。像素格式编译期策略化（Mono1、RGB565、XRGB8888 按 pixel_traits 模板化），i.MX6ULL 为 RGB565 判例机。
- 三级宿主梯队：X11 开发机日常，裸 Linux fbdev 加 evdev 是 Cinux 接口形状的彩排（D5 已通），Cinux 现场只换填表人（fb 换 Cinux 的 mmap、事件换 WireEvent、PTY 换 Cinux 的 PTY）。

## 血统纪律（Cinux-GUI 材料库，HEAD 0b0c135）

材料库本体在 `~/Cinux-GUI`（2026-07-06 停更），抄写以本体为准。借据一律写在被抄文件的头部注释，不设集中留档（2026-10-20 作者拍板）。已消费：posix_spawn（D4）、fbdev_device 与 evdev 读写姿势（D5）、VM 脚本族（D5）。仍可抄的方向：`scripts/build_fbdev_host_static.sh` 之外的静态构建思路已吸收，无余量。只读参考（设计素材，不搬代码）：`core/widget/terminal.cpp`（478 行，终端控件设计底稿）、`window_manager.cpp`、`desktop_icon.cpp`、`document/ai/DIRECTIVES.md`（flush 显示模型的设计裁决第一手材料）。绝不抄（方向相反的旧骨架）：旧 `core/host.hpp` 的 HostCore 表、`gui_core.*`、旧事件 ABI 三件、三家 SDL 宿主——旧表是 render_frame 方向（宿主画进核心），visor 是反向（核心产像素、宿主只收）。

## D5b 与 D6a 记账

- **D5b Wayland 后端**（已收官）：与 X11 平级的后端新成员，桌面代码零改动；后端、headless 判例与客户端按键重复均在 visor 上游。libwayland 按系统库待遇。
- **D6a 最小子程序试点**（2026-10-24 已落地）：私有通道第一稿（wire 协议、RemoteCanvas、posix_spawn 成窗，仓库 6dd811d）。通道降级为教学通道加 Cinux 先导，其资产由 C 系列复用。

## 合成器主线 C 系列（2026-10-24 拍板，替代原 D6b）

> 作者拍板：goal 是 X11 与 Wayland 应用上桌，走根治路线，桌面本体长成 Wayland 合成器。原 D6b（图标拉起任意二进制的注册表方案）撤编，其 spawn 泛化收进 C6；终端窗泛化降为杂项，随做随收。X11 应用经 XWayland 接待，不做 XReparentWindow 过渡路（过渡做完会被 XWayland 路线吸收，投资两次）。

形态：嵌套式起步。vesper 同时是 Wayland 服务端（招待应用）与客户端（合成结果经现有宿主表输出），宿主表一根线不动，fbdev 通道照跑，将来直连显卡（DRM）随时换输出端。libwayland-server 与 client 同族，系统库待遇（本机 1.26.0 已验），third_party 不增员。

- **C1 合成器心脏**（2026-10-24 已收官）：内嵌 libwayland-server，开 wl_compositor、wl_shm、wl_seat（空壳）、xdg_wm_base 四全局；自家最小 shm 客户端当陪练（连上、交 surface、上桌成窗）。做成判据：陪练像素逐字出现在桌面画布，wayland-info 列得出全局对象，判例 headless 全绿。三判据全过：判例在 tests/compositor，像素链判据走 FakeColorHost，wayland-info 手验于 examples/compositor_desktop 探针。
- **C2 输入**（2026-10-24 已收官）：座位充实为键盘加指针，桌面事件翻译成协议词发给持焦点的客户端，键码 Linux 原生直通。判据：陪练收到的键盘与指针事件和桌面判例喂进的一致。座位协议版本取舍：绑定 v5、滚轮走 axis_value120（保守起步已拍）。
- **C3 窗管理**（2026-10-24 已收官）：客户端报的窗接进 WindowFrame，拖动、层级、关闭、互斥焦点走桌面语义，含指针抓取。判据：双客户端同屏判例全过（拖动、层叠、关闭、抓取、焦点移交，tests/compositor/shell_desk_tests）。
- **C4 帧的礼貌**（2026-10-24 已收官）：帧回调按显示节拍发（Server::end_frame 一拍结账），缓冲合成后释放，damage 只报变化。判据：千帧单缓冲客户端零死锁、计数逐帧对齐、角块 damage 单条带（tests/compositor/cadence_tests）。
- **C5 XWayland**（2026-10-24 已收官）：rootless XWM 路线（不做 X 侧 reparent，不依赖 Xwayland 创建 xdg toplevel），xcb 与 xcb-composite 负责 Composite 重定向、WM_S0、WL_SURFACE_ID、WM_DELETE_WINDOW；桌面侧 shell role 复用窗口层。启动时持续 dispatch/flush Wayland，等待 displayfd 就绪，abstract listener 兼容 WSLg 只读 `/tmp/.X11-unix`。判据：真实 X11 程序（examples/xhello，Xlib 写）在桌面显示琥珀像素、接收关闭消息并以 0 退出；缺 Xwayland 二进制时响亮跳过。
- **C6 第四口提炼**（2026-10-24 已收官）：spawn 形状（路径、参数、环境带 WAYLAND_DISPLAY、递 fd）提炼为 visor 宿主表第四口提案，与 stage-4 联动，Cinux 侧将来同口。提案已进上游合并（2e6d25c 的 spawn-slot 第四口、4bae5d2 的 env 预算 256 词与原号 gift 摘 CLOEXEC 封条），vesper 侧回填收官，child_spawn 与 XWayland::launch 改走第四口出生，随 d7b685c 上 main。底稿在 documents/visor/spawn-fourth-slot-proposal.md。

资产复用：D6a 的通道与事件平铺、RemoteCanvas 形状（surface 进画布）、draw_image 原语在 C1 与 C3 直接上岗。

## 终端主线 T 系列（2026-10-25 拍板）

> 作者拍板：终端立为主线，长成真程序，App 与桌面分家。第一方 App 落顶层 apps/，terminal/ 打头，慢慢单开。third_party 仍只收借来的独立仓（不增员拍板不动），自家 App 将来独立成仓时再以子模块身份进 third_party。

形态：终端是独立的 Wayland 客户端进程，由桌面经第四口 child_spawn 带着 WAYLAND_DISPLAY 拉起，是 C6 出生通道的第一个真实消费者，也是将来图标启动器点开的第一个程序。分家顺着现成的缝切：Terminal 自带 TerminalSource 接口，键盘字节从 send() 出去、程序字节从 receive() 回来，头文件只依赖 visor，而桌面的 Desktop 控件只有壁纸、面板与横幅，并不知道终端存在。VT 网格（ui/terminal）、PTY 泵（ui/pty_source）、forkpty 开管（host/pty）三块迁入 apps/terminal，桌面本体留窗口宿主、摆放、session 与 compositor。

- **T1 分家立库**（2026-10-25 已收官）：terminal、pty_source、host/pty 三块迁 apps/terminal，立 vesper.terminal 静态库。落地偏离两笔，均记录在案：keymap（US 文本折叠表）一并迁入 app 仓，因为新客户端要从 evdev 键码折回码点，而桌面的 evdev_input 与 wayland_canvas 也用它，vesper.ui 经 PUBLIC 链接续用（这张表的长期家在上游 visor，随 stage-4 再议）；判例随迁 tests/apps/，单一验证入口不变。判据达成：纯搬移零逻辑改动，五道全绿。
- **T2 客户端本体**（2026-10-25 已收官）：apps/terminal/app 的 vesper_terminal 可执行，Wayland 客户端侧接线齐（连接、双缓冲 shm 画布按 release 节拍、xdg toplevel、帧回调、键码折叠写 PTY、出生时 set_winsize）。网格固定 80x24：shell 尚无 resize，窗口按出生尺寸走。判据达成：headless 判例在测试内起服务端与真程序对打，脚本 PTY 程序吐 READY，网格上屏。
- **T3 第四口出生**（2026-10-25 已收官）：出生通道取 WAYLAND_SOCKET 礼物（与 XWayland 同形，socketpair 直连，不落 XDG_RUNTIME_DIR），非原案的 WAYLAND_DISPLAY 文字；通用封装 GuestSpawn 进 vesper.compositor（launch/alive/client，可二次出生）。compositor_desktop 的 TERMINAL 图标由嵌控件改为拉进程（二进制取 VESPER_TERMINAL 或同目录 sibling）。判据达成：guest_spawn_tests（拒坏路径、/bin/true 生死与再出生）加 terminal_app_tests（出生、成窗、标题尺寸、READY 上屏、座位按键经 PTY 回显、Ctrl+D 退出、无僵尸无孤儿连接）。
- **T4 打磨收尾**（口径收窄）：滚屏携装与 SGR 判例原已收官（随 T1 迁 tests/apps）；退出路径判例并入 terminal_app_tests。resize 与 SIGWINCH 的对拍等 shell 长出 resize 再接，当前无事件来源；光标与选区随做随收（原「终端窗泛化」并入此处）。

## 与 Cinux 站 26 的接缝（本仓不碰，但要知道）

- 站 26 内核侧只供三样：fb 设备节点加 mmap、输入事件设备节点（IRQ1 键盘加 IRQ12 鼠标）、PTY（站 23 先行）。信号（站 21）是 PTY 与作业控制的前置。
- visor `bindings/cinux` 已有 FrameTarget/expand_region 与 WireEvent/translate_event，mmap 与 read 的适配层按 visor stage-4 拍板落在 CinuxOS 仓。evdev 与 WireEvent 是两个翻译层，判例各留各的。
- 桌面壳在 Cinux 侧的启动形态：「填宿主表的用户程序」，init 线程装配线点火。
- spawn 议题两仓联动：Linux 侧 D6 的经验直接喂给 visor stage-4 挂起的「第四口」与 Cinux 侧 fork/exec 的对接设计。
