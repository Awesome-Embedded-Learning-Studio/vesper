# PIN.md — 抄写留档

> 抄写纪律：每个抄来的文件在此留档（来源仓与 HEAD、抄写日期、原路径、改动摘要），一眼能对回原仓 diff。整库借壳期（未 vendor）的接法也在此说明。

## 借壳接法（未 vendor 期）

- **接法**：vesper 自己声明构建目标，编译输入直指 `VISOR_ROOT`（CMake 缓存变量，默认 `/home/charliechen/visor`）的 include 与 src。visor 仓零改动、零复制，`visor.core` / `visor.ui` / `visor.host` 三个目标在 vesper 的 CMakeLists 里声明，源码列表 `GLOB CONFIGURE_DEPENDS` 跟随活树。
- **为什么不 `add_subdirectory`**：visor 的 CMakeLists 用 `CMAKE_SOURCE_DIR` 拼 include 路径、顶层无条件 `find_package(X11 REQUIRED)`，都只在 visor 当根时成立，修它等于改 visor。
- **tested-against**：visor @ `4174f87`（feat(ui): add a full-screen popup layer，树语义束 B27–B30 终点），2026-10-18 实测全绿。两边验收同跑：visor 自家四道绿，vesper 四道绿。验收时 visor 工作树另有 dropdown 进行中修改未提交，不在束内，vesper 判例不受影响。
- **红线**：不修改 visor 树内任何文件。vesper 缺件走上游提（见 documents/visor-upgrade-design.md），不私养借壳源。
- **vendor 三前置**（达成本仓改整库 pin）：到达 Cinux-GUI 曾有高度、完成优化与基础控件、小 RAM 彩屏分块探针通过。

## 抄写清单

### src/fonts/demo_font.hpp

- **来源**：visor @ f15128e，`examples/demo_font.hpp`
- **抄写日期**：2026-10-18
- **改动摘要**：仅命名空间 `visor::examples` → `vesper::fonts`，并补本注释块。其余逐字节未动。
- **理由**：字体生成物（fontgen 的产物），示例级判例字体，桌面 D0/D1 的判例与肉眼通道都靠它。它住 visor 的 examples/，不是库目标的一部分，借壳目标拿不到，只能抄。
