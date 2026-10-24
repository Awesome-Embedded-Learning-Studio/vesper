# 桌面路线：终局形态与 D5b/D6 切法

> 自 pack-26-desktop-handover.md（2026-10-17 交接文档）瘦身而来，2026-10-20。原文的仓骨架盘点、visor 现状快照、D0-D5 已完成刀法与已拍板的岔口不再保留，这里只放还活着的路线。D0-D5 的现状见 README，通道运维见 fbdev-vm-lane.md。

## 终局形态（2026-10-17 拍板，未变）

- 桌面不只是壳，是程序启动器加窗口宿主：桌面上摆图标，图标拉起真二进制程序，每个程序一个窗口。Linux 侧 posix_spawn 拉本机程序，Cinux 侧将来 fork/exec 拉 Cinux 用户程序。这条线对应旧 `HostDesktop::spawn` 扩展口，与 visor stage-4「宿主表要不要第四口 spawn」联动，届时两边一起拍。
- 进程形态：桌面本体（窗口树、摆放、合成）单进程，但「窗口内容向桌面递像素、桌面向窗口递事件」这条边从 D0 起就以进程内接口的形状存在（递数据、不共享状态）。D6 换真进程时桌面结构不推倒重来，跨进程协议推迟到 D6 定稿。
- 产品形态：「桌面本体 + 一组 App」，完成度向 Windows 看齐但不抄设计。第一版身份原为 Mono1 黑白加圆角，2026-10-23 彩色化升级为暮金加暖纸的彩色面（七槽主题、XRGB8888 画布），圆角不变。App 从第一天对着一个 App 口写（报名字、给图标、造窗口内容、收生命周期回调），单进程期是编译进来的注册模块，D6 后同一接口换真进程。
- 生态三级：终端即命令行生态宿主（PTY 加 VT 是开放标准）；X11 后端可嵌外来窗（XReparentWindow，X11 特权）；远期长成 Wayland 合成器，以 XWayland 接待 X11 应用。自有 App 口定位教学通道加 Cinux 通道。像素格式编译期策略化（Mono1、RGB565、XRGB8888 按 pixel_traits 模板化），i.MX6ULL 为 RGB565 判例机。
- 三级宿主梯队：X11 开发机日常，裸 Linux fbdev 加 evdev 是 Cinux 接口形状的彩排（D5 已通），Cinux 现场只换填表人（fb 换 Cinux 的 mmap、事件换 WireEvent、PTY 换 Cinux 的 PTY）。

## 血统纪律（Cinux-GUI 材料库，HEAD 0b0c135）

材料库本体在 `~/Cinux-GUI`（2026-07-06 停更），抄写以本体为准。借据一律写在被抄文件的头部注释，不设集中留档（2026-10-20 作者拍板）。已消费：posix_spawn（D4）、fbdev_device 与 evdev 读写姿势（D5）、VM 脚本族（D5）。仍可抄的方向：`scripts/build_fbdev_host_static.sh` 之外的静态构建思路已吸收，无余量。只读参考（设计素材，不搬代码）：`core/widget/terminal.cpp`（478 行，终端控件设计底稿）、`window_manager.cpp`、`desktop_icon.cpp`、`document/ai/DIRECTIVES.md`（flush 显示模型的设计裁决第一手材料）。绝不抄（方向相反的旧骨架）：旧 `core/host.hpp` 的 HostCore 表、`gui_core.*`、旧事件 ABI 三件、三家 SDL 宿主——旧表是 render_frame 方向（宿主画进核心），visor 是反向（核心产像素、宿主只收）。

## D5b 与 D6 切法（待做）

- **D5b Wayland 后端**：与 X11 平级的后端新成员，桌面代码零改动。Mono1 需扩成 32 位色交付，复用 visor cinux 绑定的扩色。判例走 headless 合成器（weston 或 sway，开发机未装，届时补）。libwayland 按系统库待遇。注意 Wayland 禁止客户端互相嵌窗，D6 拉起的程序只能自己找合成器开窗、与本桌面平级，宿主愿景在这条后端上打折。
- **D6a 最小子程序试点**（2026-10-24 已落地）：只会画方块的最小子程序走私有通道把像素递进桌面成窗，跨进程协议第一稿探完路（wire 协议、RemoteCanvas、posix_spawn 成窗，详见仓库 6dd811d）。
- **D6b 任意程序拉起**：图标点击拉起本机任意二进制成窗（Linux 侧先做），终局形态的第一块，给「宿主表第四口」议题供判例。spawn 口的表形态等 D6a 判例再议，与 visor stage-4 联动。

## 与 Cinux 站 26 的接缝（本仓不碰，但要知道）

- 站 26 内核侧只供三样：fb 设备节点加 mmap、输入事件设备节点（IRQ1 键盘加 IRQ12 鼠标）、PTY（站 23 先行）。信号（站 21）是 PTY 与作业控制的前置。
- visor `bindings/cinux` 已有 FrameTarget/expand_region 与 WireEvent/translate_event，mmap 与 read 的适配层按 visor stage-4 拍板落在 CinuxOS 仓。evdev 与 WireEvent 是两个翻译层，判例各留各的。
- 桌面壳在 Cinux 侧的启动形态：「填宿主表的用户程序」，init 线程装配线点火。
- spawn 议题两仓联动：Linux 侧 D6 的经验直接喂给 visor stage-4 挂起的「第四口」与 Cinux 侧 fork/exec 的对接设计。
