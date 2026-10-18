# 桌面独立仓 · 开工交接文档

> 交接日 2026-10-17。本文档面向「直接开工」的新会话：桌面层从 Cinux 与 visor 两仓独立出来，自成一仓。决策背景与接缝记录在 `pack-26.md` §6.8 与 AU8，本文档是它的工程展开。仓名已定 `vesper`（2026-10-17 作者拍板），仓库位于 `~/vesper`，与 visor 平级。

## 1. 定位与最终形态

桌面层独立成仓，KDE 式起头：一个纯用户态工程，消费 visor（填它的宿主表），产出可拖窗口的桌面加终端。它从第一天起就不碰内核，这件事本身就是站 26 的教学论据「GUI 离操作系统很远」的活证明。

终局形态（作者愿景）：桌面上摆着图标，图标拉起的是真正的二进制程序，每个程序一个窗口，酷酷的那种。也就是说桌面不只是壳，它是程序启动器加窗口宿主：Linux 侧用 posix_spawn 拉起本机程序，Cinux 侧将来用 fork/exec 拉起 Cinux 用户程序。这条终局线对应旧仓 `HostDesktop::spawn` 扩展口（visor 的 stage-4 文档里「宿主表要不要第四口 spawn」正是同一个议题，两边届时一起拍）。

进程形态（2026-10-17 拍板）：桌面本体（窗口树、摆放、合成）保持单进程，不因终局是多进程就从第一天拆进程。但「窗口内容向桌面递像素、桌面向窗口递事件」这条边界从 D0 起就以进程内接口的形状存在，与宿主表同款纪律：递数据、不共享状态。D6 把这条线换成真进程时桌面结构不推倒重来。像素与事件的跨进程协议推迟到 D6 定稿，因为窗口层叠、拖拽归属、键盘焦点这些语义要等 D1-D3 长出来，先定必返工。

产品形态（2026-10-17 拍板）：终局是「桌面本体 + 一组 App」的成熟桌面，完成度向 Windows 看齐，但不照抄 Windows 或 macOS 的设计。借鉴概念时在文档里留 reference 标签（如「概念参考 Windows 任务栏」），视觉语言走自己的路：第一版 Mono1 黑白加圆角，单色风就是身份。App 从第一天就对着一个 App 口写（报名字、给图标、造窗口内容、收生命周期回调），单进程期是编译进来的注册模块，D6 后同一接口换成真进程，每新增一个 App 都是对这条边界的判例。

生态与像素（2026-10-17 增补）：生态口走开放标准，分三级。第一级终端即命令行生态宿主（PTY 加 VT 本身是开放标准）。第二级 X11 后端可嵌外来窗（XReparentWindow），明写为 X11 特权。第三级远期长成 Wayland 合成器（服务端侧），以 XWayland 接待 X11 应用。自有 App 口定位教学通道加 Cinux 通道，协议文档公开。像素格式为编译期策略（Mono1、RGB565、XRGB8888 三档按 pixel_traits 模板化），i.MX6ULL 为 RGB565 判例机。上游提案见 documents/visor-upgrade-design.md，给 visor 会话的引导在其仓 document/todo/vesper-brief.md。

## 2. 三级宿主梯队（本仓的产品主线）

| 梯队 | 环境 | 作用 |
|---|---|---|
| X11 | 开发机日常（WSLg 的 `DISPLAY=:0`） | 开发主战场，判例跑 Xvfb 无头 |
| 裸 Linux fbdev + evdev | KVM 虚拟机，控制台启动，无桌面环境 | Cinux 接口形状的彩排：`/dev/fb0` 直接 mmap、输入直接读设备节点，中间没有合成器帮忙 |
| Cinux | 站 26 现场 | 只换填表人：fb 换成 Cinux 的 fb mmap，事件换 WireEvent，PTY 换 Cinux 的 PTY |

裸环境 VM 的两个候选做法（可并存）：轻量首选是自组 initramfs（busybox 加静态编译的桌面二进制，`/init` 直接拉起桌面，形状最接近 Cinux，旧仓有静态构建脚本先例 `scripts/build_fbdev_host_static.sh`）；备选是 Alpine virt 光盘进控制台。两者都走 `qemu-system-x86_64 -accel kvm`，KVM 缺席就大声失败（Cinux 仓同款红线，不回退 TCG）。

主线梯队之外另有后端增员：Wayland（2026-10-17 增补）。它与 X11 同性质，是开发舒适位，不属于奔向 Cinux 的梯队，切法见 §6 的 D5b。

## 3. 血统纪律：抄什么、读什么、绝不抄什么

材料库是 `~/Cinux-GUI`（HEAD `0b0c135`，2026-07-06 停更，工作树干净）。注意：Cinux 主仓 `.notes/materials/cinux-gui` 快照只有文档没有源码，抄写一律以本体为准。

**可抄（方向无关的底层接线，逐文件留 pin 记录）**

| 旧仓文件 | 拿来做什么 |
|---|---|
| `host/fbdev_device.hpp/.cpp` | fbdev 开窗、ioctl 取分辨率、mmap、脏区逐行 memcpy，缝很干净 |
| `host/evdev_device.hpp/.cpp` | `EvdevAccumulator` 是纯状态机（无 fd 可单测），读循环带 `EV_SYN` 分帧、`SYN_DROPPED` 复位、`EVIOCGRAB` 抢占 |
| `host/linux_fbdev_main.cpp` | 装配样例（开设备、填表、主循环），抄时要改写成填 visor 的表 |
| `host/posix_spawn.cpp` | `linux_spawn`：forkpty 加 execvp，PTY 终端拉真 shell 就靠它 |
| `scripts/build_fbdev_host_static.sh` | 全静态构建思路（initramfs 用） |

**只读参考（设计素材，不搬代码）**：`core/widget/window.cpp`（330 行）、`core/widget/window_manager.cpp`（322 行）、`core/widget/terminal.cpp`（478 行，终端控件的设计底稿）、`core/widget/desktop_icon.cpp`、`document/ai/DIRECTIVES.md`（flush 显示模型的设计裁决第一手材料）。

**绝不抄（方向相反的骨架）**：`core/host.hpp` 的 `HostCore` 表、`gui_core.*`、`core/event.hpp` 加 `event_payload.hpp` 加 `abi_check.cpp`（旧事件 ABI），以及三家 SDL 宿主（它们不走表直连 Desktop）。原因：旧表方向是 `render_frame`（宿主把场景画进核心的 staging），visor 的表是反向的（核心产出像素、宿主只接收，`FlushRegion::pixels` 是 const）。把旧骨架搬进来等于把教学上要避开的旧形态带进新仓。

**pin 记录格式**（每个抄来的文件在仓内 `PIN.md` 或文件头留档）：来源仓与 HEAD（`Cinux-GUI@0b0c135`）、抄写日期、原路径、改动摘要。目标是一眼能对回原仓 diff。

## 4. visor 侧现状（依赖面盘点，HEAD `9294df8`）

**宿主表**（`include/visor/bindings/host.hpp`，namespace `visor::host`）：三口加 context——

- `flush(context, const FlushRegion&)`：`FlushRegion{rect, const uint8_t* pixels, stride_bytes, PixelFormat::Mono1}`
- `poll_event(context, Event&) -> bool`：无事件返回 false，不阻塞
- `clock_ms(context) -> uint64_t`：毫秒，可回绕

判空包装在同头文件（`submit_region`/`submit_frame`/`try_poll_event`/`current_time_ms`），空表退化不终止。CMake 目标已有 `visor.host.x11`、`visor.host.cinux` 等，新仓借壳开发时直接把这些目标当构建输入。

**事件**（`bindings/events.hpp`）：`std::variant<KeyDown, KeyUp, PointerMove, PointerDown, PointerUp>`，trivially copyable。方向键取带外值 0x110..0x113，可打印键走码点，空格的 32 与激活的 57 各读各的。

**装配样板**（`examples/x11_window.cpp`，新宿主照这个长）：`X11WindowHost::open(nullptr, w, h, scale)` → 拿 `table()` → 栈上 `InputRouter` 加 `DirtyRegions` → `set_dirty_sink` → `dirty.push(kScreenBounds)` 抵首帧 → `while (!host.close_requested()) { run_dirty(host.table(), panel, input, frame, kScreen, dirty); sleep 15ms; }`。

**运行循环**（`ui/run_loop.hpp`）：`run_dirty(host, root, input, strip, screen, dirty)` 一轮管线（泵事件、分发、重画欠的条带、清脏表）。轮次由调用者驱动，循环内置零阻塞，指针事件一轮抽干、键事件一轮最多一个。桌面仓的事件循环在这外面再包一层 poll（见 §7 D4 的多源编排）。

**拖拽与点击**：`InputRouter` 捕获语义现成（PointerDown 落谁身上谁持有指针，之后 move/up 都归他），Slider 是第一个拖拽消费者，Button 有松手落回界内才开火的点击合成。**双击手势没有**，events.hpp 明说手势是五事件之上将来长的层。

**控件**：八个控件（Rectangle/Button/Checkbox/Label/Slider/TextBox/RadioGroup/Dropdown）加 HBox/VBox。注意 `Widget::kMaxChildren = 8` 是定容子表，桌面上图标加窗口轻松超八，Desktop 容器要么内部嵌套、要么回 visor 议扩容，这是要早拍的一件事。

**渲染面**：当前只有 Mono1（1bpp 黑白），主题与圆角现成（`fill_rounded`）。RGB565 彩色在 visor 第三站，分块探针过前渲染接口不冻结——桌面仓第一版就按黑白做，不被彩色卡路。

**测试判例照抄对象**：带真窗口的宿主判例看 `tests/x11_host_tests.cpp`（Xvfb 读回差分，Xorg 吞 XSendEvent 所以走进程内队列注词）；纯翻译逻辑看 `tests/x11_tests.cpp` 与 `tests/cinux_tests.cpp`；宿主表语义看 `tests/host_tests.cpp`（OffscreenHost）。测试框架是 `run_xxx_tests()` 报名进 `cases.hpp`，断言用 `VISOR_CHECK`/`VISOR_CHECK_EQ`，新文件要补进 `tests/CMakeLists.txt`。

**工程规范**（新仓直接继承 visor 的口径）：`-fno-exceptions -fno-rtti -Wall -Wextra -Werror`，C++23，零第三方依赖（Xlib 算系统库），构建不用 presets，注释一律英文，新码先查 `visor::base` 词汇（几何/位库/RingQueue/UTF-8），验收一条命令跑全部检查（visor 是六道，新仓照抄结构改内容）。

## 5. 抄写时注意的两个技术岔口

1. **鼠标坐标制式**。旧仓 `EvdevAccumulator` 只处理 `EV_ABS`（绝对坐标）。QEMU 虚拟机里 PS/2 鼠标报的是 `EV_REL`（相对位移），绝对坐标要靠 `-device usb-tablet` 一类让 QEMU 出绝对值设备，或者给累积器扩相对分支。裸环境 VM 配置里必须显式选一条，这是第一个会当场看见的岔口。
2. **键盘路线**。旧仓 fbdev 宿主只有指针没有键盘。键盘走 evdev 的 AT 键盘节点报 set-1 scancode，visor `bindings/cinux/event_wire.hpp` 里的 `named_key_for` 表正好是 set-1 译码（Backspace/Tab/Enter/Space/四方向），fbdev 宿主的键盘翻译层照这张表的思路写，ASCII 译文由宿主层补（evdev 只给 scancode）。

## 6. 里程碑切法（每刀带验收）

- **D0 仓骨架**：目录、CMake、CLAUDE.md、验收脚本、PIN.md 格式立好；visor 以 `VISOR_ROOT` 源码借壳接入（构建输入直指其 include/src），vendor 三前置达成后改整库 pin。
- **D1 WindowFrame 加拖拽**（X11）：标题栏吃 InputRouter 捕获语义，拖动=改 bounds 新旧两框报欠画。判例：拖动后帧差断言（仿 x11_host_tests 的读回比对）。
- **D2 Desktop 容器加图标加双击**：自由定位容器（子表容量问题在此拍）、图标（Label 加 MonoBitmap）、双击判定（若决定手势归 visor 则此时回上游提，不私养一份）。判例：双击开窗的纯逻辑案。
- **D3 Terminal 控件加假源**：字符网格加光标加回显，数据源用回环假源跑离屏判例（80×25 起步，ANSI 先不做）。
- **D4 真 bash 走 PTY**：抄 `linux_spawn`，事件循环扩成 poll 编排（event0 等价物加各终端 PTY master，谁有货醒谁，队列空则睡）。ANSI 子集按 bash 实际输出补：提示符重绘、清屏、光标定位，不贪全。判例：PTY 回环案（写已知串读回渲染帧）。
- **D5 fbdev 宿主加裸环境 VM**：抄 `fbdev_device` 加 `evdev_device`，initramfs 或 Alpine 二选一先通一条；鼠标坐标制式岔口在此收口。判例：VM 内跑桌面，QEMU 截屏断言亮像素（Cinux 仓 mcp 的 screendump 同款手法）。
- **D5b Wayland 后端**：与 X11 平级的后端层新成员，桌面代码零改动（宿主表天生与显示系统无关）。Mono1 需扩成 32 位色交付，复用 visor cinux 绑定的 1bpp 到 XRGB8888 扩色。判例走 headless 合成器（weston 或 sway，开发机当前未装，届时补）。libwayland 按系统库待遇，与 Xlib 同列。注意 Wayland 禁止客户端互相嵌窗，D6 拉起的程序只能自己找合成器开窗、与本桌面平级，宿主愿景在这条后端上打折。
- **D6a 最小子程序试点**：拉一个只会画方块的最小子程序，走私有通道把像素递进桌面成窗。多进程的真实感在 D4 收刀后即可摸到，跨进程协议第一稿由它探路。
- **D6b 任意程序拉起**：图标点击拉起本机任意二进制成窗（Linux 侧先做），这是终局形态的第一块，也给「宿主表第四口」议题供判例。

每刀收尾跑整仓验收（结构仿 `visor/scripts/checks/acceptance.sh`：普通、ASan/UBSan、TSan、format、tidy，fbdev 相关检查在无 `/dev/fb0` 的开发机上跳过实弹只跑逻辑案，VM 实弹单列）。

## 7. 与 Cinux 站 26 的接缝（本仓不碰，但要知道）

- 站 26 内核侧只供三样：fb 设备节点加 mmap、输入事件设备节点（IRQ1 键盘加 IRQ12 鼠标）、PTY（站 23 先行）。信号（站 21）是 PTY 与作业控制的前置。
- visor 的 `bindings/cinux` 已有 `FrameTarget/expand_region`（1bpp 到 XRGB8888）与 `WireEvent/translate_event`（定长 scancode 加鼠标包），mmap 与 read 的适配层按 visor stage-4 的拍板落在 CinuxOS 仓。evdev 与 WireEvent 是两个翻译层，判例各留各的，不共用。
- 桌面壳在 Cinux 侧的启动形态：站 26 章内它是「填宿主表的用户程序」，init 线程装配线点火（Cinux 仓 pack-26 §3 有落法倾向）。
- spawn 议题两仓联动：Linux 侧 D6 的经验直接喂给 visor stage-4 挂起的「第四口」与 Cinux 侧 fork/exec 的对接设计。

## 8. 分工边界与待同步事项

- 新仓独立会话主理（同 visor 模式）；Cinux 仓 `pack-26.md` §6.8 加 AU8 只记接缝不记实施。
- 开工时给 visor 会话打个招呼：`document/todo/stage-x-pc-window.md` 明写「桌面语义（WM、终端）不随本线开工，仍是第四站主菜」，新仓成立后这句要改口；`stage-4-cinux.md` 里「桌面层与 PC 窗口宿主谁先谁后未定」一行疑似没随 2026-10-14 的拍板同步，一并提。
- 本仓与 visor 的「互不回搬」边界沿既有拍板：pin 之后两边各自演化，桌面仓发现 visor 缺件（双击手势、子表容量、彩色）走上游提，不私改借壳源。

## 9. 未决问题清单（开工时逐个拍）

1. 仓名与仓库位置：已拍板，`vesper`，位于 `~/vesper` 与 visor 平级（2026-10-17）。
2. `Widget::kMaxChildren = 8` 对 Desktop 够不够：嵌套容器还是回上游扩容。
3. 双击手势归属：桌面仓私有还是 visor 手势层。
4. 裸环境 VM 走 initramfs 还是 Alpine（倾向 initramfs 先行）。
5. 鼠标坐标制式：usb-tablet 出绝对值，还是累积器扩 EV_REL 分支。
6. spawn 口在宿主表的形态（D6 已拆 D6a/D6b：最小子程序先探协议，任意程序随后。等 D6a 判例再议，与 visor stage-4 联动）。
