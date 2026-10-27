# vesper

用 C++23 从零实现的桌面环境，[Awesome-Embedded-Learning-Studio](https://github.com/Awesome-Embedded-Learning-Studio) 的教学项目。当前长出了桌面本体、内建终端，以及一套自研 Wayland 合成器。

## 这是什么

vesper 的目标是长成「桌面本体 + 一组应用」的形态：桌面承担程序启动器和窗口宿主，图标拉起真实的程序进程，每个程序一个窗口。视觉走单色（黑白）加圆角的自有风格。

界面绘制与设备接入的底层抽象复用同组织的 [visor](https://github.com/Awesome-Embedded-Learning-Studio/visor)

## 功能

- 桌面本体：可拖动窗口、桌面图标双击启动程序、单击选中。
- 内建终端：字符网格终端，通过伪终端（PTY）拉起真实 bash，支持 ANSI 十六色，窗口缩放时网格跟随重排。
- Wayland 合成器：内嵌 libwayland-server，实现 wl_compositor、wl_shm、wl_seat、xdg_wm_base，Wayland 客户端以原生窗口进桌面。
- XWayland：X11 应用经 rootless XWayland 与原生窗口同层显示，支持 WM_DELETE_WINDOW 优雅关窗。
- 裸机通道：直接面向 fbdev（Linux 的 framebuffer 显示设备）与 evdev（输入事件设备），配合自组 initramfs（内存根文件系统）在 QEMU + KVM 虚拟机里跑真实键鼠交互。

## 环境要求

- Linux，CMake ≥ 3.28，Ninja，支持 C++23 的编译器。
- pkg-config，以及这些库的开发包：wayland（client 与 server 各 ≥ 1.26）、wayland-protocols ≥ 1.30、xcb、xcb-composite。
- X11 开发包（可选）：缺了只影响 X11 调试目标，不影响库与测试。
- 裸机冒烟另需 QEMU，并启用 KVM（脚本在 KVM 缺席时直接报错，不做纯模拟降级）。

## 构建

全仓以 C++23 编译，统一开 `-fno-exceptions -fno-rtti -Wall -Wextra -Werror`，除上面列的系统库外零第三方依赖。

```bash
git clone --recurse-submodules https://github.com/Awesome-Embedded-Learning-Studio/vesper.git
cd vesper
cmake -S . -B build -G Ninja
cmake --build build
```

## 测试

全部测试跑在进程内离屏宿主上，不需要显示器：

```bash
ctest --test-dir build --output-on-failure
```

### 裸机冒烟（可选）

在 QEMU 虚拟机里启动自建内核加 initramfs，从串口日志找标记、对屏幕截图做像素断言。内核与 busybox 源码由脚本自行下载固定版本。首次按序：

```bash
scripts/vm/build_kernel.sh
scripts/vm/build_busybox.sh
scripts/vm/build_static.sh
scripts/vm/build_initramfs.sh
INPUT=1 scripts/vm/smoke.sh
```

`INPUT=1` 打开真实键鼠交互的自动化检查，经 VNC（`127.0.0.1:5999`）注入双击、拖动、关窗等操作，分步截图与串口日志保存在 `build/vm/run/`。不带 `INPUT` 只验启动和首帧。内核与 busybox 构建后有缓存，重复跑从 `build_static.sh` 开始即可。

## 运行

| 形态       | 怎么跑                                                                                     | 说明                     |
| ---------- | ------------------------------------------------------------------------------------------ | ------------------------ |
| 进程内离屏 | `ctest`（见上）                                                                            | 测试全在此形态           |
| X11 窗口   | `cmake --build build --target x11_desktop`，然后 `DISPLAY=:0 ./build/examples/x11_desktop` | 最方便的肉眼调试通道     |
| 裸机 VM    | `scripts/vm/smoke.sh`                                                                      | 不依赖桌面环境的真实通道 |

WSLg 下直接用 `DISPLAY=:0`。走 `localhost:0` 的远程转发链路时窗口可能映射不上，查询类客户端（xdpyinfo、xwininfo）会卡死，直连可解。

## 文档

[documents/research/](documents/research/) 收着八篇桌面环境调研，从窗口协议、KDE 与 GNOME 的架构，到终端模拟器与最小应用集，面向想了解桌面构造的读者，与仓库代码相互独立。

## 许可证

[MIT](LICENSE)
