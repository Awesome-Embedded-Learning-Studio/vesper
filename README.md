# vesper

单色桌面壳，KDE 式起头的教学桌面工程。消费 visor（填它的宿主表），产出可拖窗口的桌面。终局形态是「桌面本体 + 一组 App」：桌面是程序启动器加窗口宿主，图标拉起真二进制，每个程序一个窗口。第一版视觉走 Mono1 黑白加圆角的自己的路。

## 现状

- **D0 仓骨架**：目录、CMake 以 `VISOR_ROOT` 借壳接入 visor（不改 visor 树，接法见 PIN.md）、fake session host（固定尺寸整屏、事件队列、区域记录）、最小桌面（根 Box 加一行 Label）离屏判例。
- **D1 WindowFrame**：标题栏拖动（走 visor InputRouter 的捕获语义）、关闭键收回窗口、拖动与关闭的重画区域判例。X11 只做肉眼调试。

## 三级宿主梯队

| 梯队 | 环境 | 作用 |
|---|---|---|
| fake session host | 进程内离屏 | 验收主战场，判例都跑它 |
| X11 | 开发机 WSLg 的 `DISPLAY=:0` | 肉眼调试，不进验收 |
| 裸 Linux fbdev + evdev、Cinux | KVM 虚拟机、站 26 现场 | D5 起，只换填表人 |

## 构建

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## 验收

```bash
scripts/checks/acceptance.sh
```

四道：rel 构建与测试、ASan/UBSan 构建与测试、clang-format、clang-tidy。X11 肉眼通道不进验收，想看就跑：

```bash
cmake --build build --target x11_desktop
DISPLAY=:0 ./build/examples/x11_desktop
```

WSLg 下直接挂 `DISPLAY=:0`（XWayland 直连）。走 `localhost:0` 那条远程转发链路时窗口可能映射不上，查询类客户端（xdpyinfo、xwininfo）也会卡死，直连可解。

## 工程规范

C++23，`-fno-exceptions -fno-rtti -Wall -Wextra -Werror`，零第三方依赖。开发工作流见 CLAUDE.md，借壳接法与抄写留档见 PIN.md，路线见 pack-26-desktop-handover.md。
