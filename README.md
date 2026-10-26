# vesper

单色桌面壳，KDE 式起头的教学桌面工程。消费 visor（填它的宿主表），产出可拖窗口的桌面。终局形态是「桌面本体 + 一组 App」：桌面是程序启动器加窗口宿主，图标拉起真二进制，每个程序一个窗口。第一版视觉走 Mono1 黑白加圆角的自己的路。

## 现状

- **D0 仓骨架**：目录、CMake 以 `VISOR_ROOT` 借壳接入 visor（不改 visor 树，接法见 CLAUDE.md 借壳红线）、fake session host（固定尺寸整屏、事件队列、区域记录）、最小桌面（根 Box 加一行 Label）离屏判例。
- **D1 WindowFrame**：标题栏拖动（走 visor InputRouter 的捕获语义）、关闭键收回窗口、拖动与关闭的重画区域判例。X11 只做肉眼调试。
- **D2 图标与双击**：图标控件（位图加说明文字）、双击开窗（手势吃 visor 路由层的）、单击选中环、桌面墙纸口（吃 P4 位图）。
- **D3 终端控件**：字符网格加光标加回显，回环假源离屏判例（80×25，ANSI 先不做）。
- **D4 真 shell 走 PTY**：forkpty 拉 bash、ANSI 三件（擦行、清屏、定位）、poll 睡法的主循环、三张自栅格化字模，验收从四道变五道（补 TSan）。
- **D5 fbdev 宿主加裸环境 VM**：fbdev 设备（Mono1 经 visor cinux 扩色进 32bpp 帧）、evdev 输入（visor 翻译层加绝对值折算加 US 键位表）、自组 initramfs（busybox 加静态桌面二进制）、QEMU+KVM 实弹判例（串口标记加截屏像素断言），脚本在 `scripts/vm/`。
- **C1 合成器心脏**：内嵌 libwayland-server（系统库待遇，scanner 从系统 XML 生成 xdg-shell 双端代码），开 wl_compositor、wl_shm、wl_seat、xdg_wm_base 四全局；shm 池与缓冲、surface 提交状态机、xdg toplevel 生命周期全链可判（陪练 shm 客户端进程内对打，像素经 FakeColorHost 逐字读回），wayland-info 实弹列得出全局，探针在 `examples/compositor_desktop`。
- **C2 输入**：座位报键盘加指针能力，键盘对象随附 evdev+qwerty 文本键位表，键码 Linux 原生直通（visor 命名方向键折回 evdev 号），修饰键映 xkb 掩码；指针走 v5，滚轮 axis_value120 每档 120 逐轴带 frame；焦点隔离与画布转发（本地坐标折叠）全判例。
- **C3 窗管理**：ShellDesk 把映射的 toplevel 接进 WindowFrame（瀑布摆位、抬升、互斥焦点、指针跟随换靶、树焦点收回），关闭按钮只发 toplevel close 由宾客自别；隐式抓取继承路由器捕获，判例覆盖拖动、层叠、关闭全链。
- **C4 帧的礼貌**：帧回调与缓冲释放归 `Server::end_frame` 按桌面节拍发（settle 只递词不结账），缓冲合成即释放（单缓冲客户端千帧零死锁判例），宾客 damage 折进画布局部重画债（角块只触发单条带）。
- **C5 XWayland**：rootless XWayland 桥用 WAYLAND_SOCKET 递连接、abstract X socket 避开 WSLg 的只读目录，持续服务 Wayland 直到 displayfd 报告就绪。XWM（xcb、xcb-composite 系统库）负责 Composite 重定向、WM_S0、WL_SURFACE_ID 窗口关联与 WM_DELETE_WINDOW 优雅关窗；X 窗口挂桌面侧 shell role，与原生 xdg 窗共用桌面窗口层。实弹判例用 `examples/xhello` 检查像素与正常退出，缺 Xwayland 时响亮跳过。
- **C6 第四口收官**：宿主表第四口 spawn（路径、argv、env、递 fd）已落上游 visor（spawn-slot 并入 main，env 预算与 CLOEXEC 补件候合并），本仓 child_spawn 与 XWayland::launch 均经第四口出生（提案底稿销案后已移除，落地偏离见其历史提交）。

## 三级宿主梯队

| 梯队 | 环境 | 作用 |
|---|---|---|
| fake session host | 进程内离屏 | 验收主战场，判例都跑它 |
| X11 | 开发机 WSLg 的 `DISPLAY=:0` | 肉眼调试，不进验收 |
| 裸 Linux fbdev + evdev、Cinux | KVM 虚拟机、站 26 现场 | D5 起，只换填表人 |

## 构建

依赖 [visor](https://github.com/Awesome-Embedded-Learning-Studio/visor)，以子模块钉在 `third_party/visor`。

```bash
git clone --recurse-submodules <本仓地址>
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## 验收

```bash
scripts/checks/acceptance.sh
```

五道：rel 构建与测试、ASan/UBSan 构建与测试、TSan 构建与测试、clang-format、clang-tidy。fbdev 实弹（QEMU+KVM 裸机彩排）单列在 `scripts/vm/smoke.sh`（`INPUT=1` 加真 VNC 键鼠交互判例），首次跑先按序执行 `scripts/vm/` 下的 build_kernel、build_busybox、build_static、build_initramfs。X11 肉眼通道不进验收，想看就跑：

```bash
cmake --build build --target x11_desktop
DISPLAY=:0 ./build/examples/x11_desktop
```

fbdev 的真实键鼠回归通过本机 VNC 注入，覆盖双击开终端、越界捕获、反向拖动、松手、执行 shell 命令和关闭窗口；每步用 QEMU 截图验证，Python 只用标准库。内核与 busybox 缓存好后运行：

```bash
scripts/vm/build_static.sh
VESPER_FBDEV_TRACE=1 FBDEV_TIMEOUT=30 scripts/vm/build_initramfs.sh
INPUT=1 scripts/vm/smoke.sh
```

该命令自动启动和收回 VM，使用 `127.0.0.1:5999`，串口与分步截图保存在 `build/vm/run/`。只验启动和首帧时仍用 `scripts/vm/smoke.sh`；跟踪仅在 `VESPER_FBDEV_TRACE=1` 时开启。桌面在实际 framebuffer 左上角按 1:1 绘制，tablet 按整个 framebuffer 换算，桌面外的坐标保持在桌面外。

WSLg 下直接挂 `DISPLAY=:0`（XWayland 直连）。走 `localhost:0` 那条远程转发链路时窗口可能映射不上，查询类客户端（xdpyinfo、xwininfo）也会卡死，直连可解。

## 工程规范

C++23，`-fno-exceptions -fno-rtti -Wall -Wextra -Werror`，零第三方依赖。开发工作流、借壳接法与 tested-against 见 CLAUDE.md，路线已移出仓库（私有携带），抄写借据写在各文件头部注释。
