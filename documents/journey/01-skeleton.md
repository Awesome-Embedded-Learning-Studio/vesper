# 第一章 起步与骨架：在借来的树上开工

这一章不写一行窗口代码。在第一扇窗口动起来之前，我们要先把三件事立起来：怎么借用 visor 这棵树而不动它一个字节，怎么在进程里造一个能当判例战场的假宿主，怎么让「桌面」作为一个普通控件在树上落根。连同这章一起进仓的还有一条验收闸门，每写一批就能用一条命令验证一批。

vesper 是教学桌面工程，目标是从零长出「桌面本体加一组 App」。但它不从零写控件库：几何、画布、控件树、输入路由这些词汇归 visor，桌面语义（窗口、拖动、启动器、将来的子进程）归 vesper。所以第一天面对的问题不是「怎么画像素」，那是 visor 教程第一章已经走过的路。第一天的问题是：两棵树怎么相处。

本章按仓库第一笔提交的工序带你走一遍。

## 借壳：直接编译它的源文件

vesper 需要链接 visor 的库。最直觉的答案是在自己的 CMake 里 `add_subdirectory` 指向 visor，第一天我们试过，它不成立，原因写在顶层 CMakeLists 的注释里：

```cmake
# The desktop borrows the visor tree in place: this cache variable points at it,
# the targets below compile its sources straight from there, and nothing inside
# the visor tree is modified or copied. add_subdirectory is not an option on
# purpose -- visor's own CMakeLists resolves its include path against
# CMAKE_SOURCE_DIR and requires X11 unconditionally, both of which only hold
# while visor is the root project.
```

visor 自己的 CMakeLists 按 `CMAKE_SOURCE_DIR` 解析 include 路径，还无条件要求 X11。这两条只在 visor 是根工程时成立。被 add_subdirectory 挂进来时它不是根工程，路径解析就断了。要让它当子目录能用，就得改它的 CMakeLists，而借壳的第一条纪律是不修改借来的树。缺件走上游提，不私养分叉。

所以接法换了一种：不挂子目录，自己的目标直接把 visor 的源文件编进来。

```cmake
set(VISOR_ROOT "/home/charliechen/visor" CACHE PATH "visor source tree borrowed in place")
if(NOT EXISTS "${VISOR_ROOT}/include/visor/bindings/host.hpp")
    message(FATAL_ERROR "VISOR_ROOT (${VISOR_ROOT}) carries no include/visor: a wrong tree was pointed at")
endif()
```

一个缓存变量指路，跟一条硬校验：树里必须真的有 `include/visor/bindings/host.hpp`，没有就当场报错停下。指错树的错误在 configure 阶段就该响，不该等到链接期冒一堆 undefined reference 让人猜。

然后是三个目标，把 visor 按层借进来：

```cmake
add_library(visor.core INTERFACE)
target_include_directories(visor.core INTERFACE ${VISOR_ROOT}/include)

file(GLOB_RECURSE VESPER_VISOR_UI_SOURCES CONFIGURE_DEPENDS ${VISOR_ROOT}/src/ui/*.cpp)
add_library(visor.ui STATIC ${VESPER_VISOR_UI_SOURCES})
target_link_libraries(visor.ui PUBLIC visor.core)

add_library(visor.host STATIC ${VISOR_ROOT}/src/bindings/host.cpp)
target_link_libraries(visor.host PUBLIC visor.core)
```

`visor.core` 只是一个 INTERFACE 目标，管 include 路径。`visor.ui` 用 `GLOB_RECURSE` 把 UI 源文件全收进来，`CONFIGURE_DEPENDS` 让 visor 那边加了新文件、这边重新 configure 时自己跟上。借用方不逐一列文件，列了就变成两处维护同一份清单。

值得停一下的是检查目标怎么划范围。format 与 tidy 的 glob 只扫 vesper 自己的源：

```cmake
file(GLOB_RECURSE VESPER_FORMAT_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/src/*.hpp
    ...)
```

visor 在它自己的仓库里跑它自己的检查，两边互不越界。把别人的源卷进自己的 tidy，等于替邻居扫地，扫出来的口径分歧还没处说理。

这套 VISOR_ROOT 接法是过渡期的样子，第七章会把它换成 git 子模块，变量消失、指针进版本库。第一天先用最轻的接法把桌面跑起来。

## 三个旗子与两种消毒剂

编译旗子跟 visor 同款：

```cmake
add_compile_options(-fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra -Werror)
```

禁异常、禁 RTTI、禁静态变量线程防护。桌面的终点里有裸机现场，异常和运行时类型信息在那边是奢侈品，从第一天就活在裸机也接受的子集里，比将来回来改写法便宜。`-Werror` 把警告当错误，教学工程的警告一旦放过，下一批人就会把它当成「本来就有的」。

两个消毒剂开关是 vesper 自己加的戏：

```cmake
option(VESPER_SANITIZE "ASan+UBSan switch" OFF)
option(VESPER_TSAN "TSan switch" OFF)

# The two sanitizers are configure-time exclusive: their runtimes cannot load
# into one process, so the conflict fails here instead of at link time.
if(VESPER_SANITIZE AND VESPER_TSAN)
    message(FATAL_ERROR "VESPER_SANITIZE and VESPER_TSAN are exclusive: separate build dirs, one switch each")
endif()
```

ASan 与 TSan 的运行时不能装进同一个进程，两个开关同时打开是配置错误，这条错误在 configure 期报出来，比链接期的谜语好读得多。每种消毒剂一个独立构建目录，普通目录保持干净。第六章终端接上真进程后，TSan 会升级为验收的固定一道。

## fake session host：判例要边界侧的证人

visor 有现成的 OffscreenHost，保最终帧加两个计数器。桌面另造了一个 FakeSessionHost，头文件注释把理由说得很直白：

```cpp
// visor's OffscreenHost keeps the final frame and two counters; the desktop's
// judgments need more, because a drag must prove the old and the new frame
// boxes were both repainted. The double therefore keeps the per-flush region
// log beside the frame, and the log is the boundary-side witness for every
// repaint claim a test makes.
```

第二章的拖动判例要主张「窗口从 A 挪到 B，新旧两个位置都重画了」。只看最终帧证明不了这件事：新位置画了，旧位置的残影可能还留在帧里，也可能压根没被提交过，两种世界最终帧长得一样。所以替身在帧之外还记逐次提交的区域日志，每一次 flush 收了哪块矩形，按到达顺序全录。重画的主张从此有边界侧的证人，不用信控件自己的说法。

替身的形状照着将来真的会话长：

```cpp
// The shape follows the Cinux session to come: one whole screen of fixed
// size, an in-process event queue, HostTable method calls, no terminal and
// no PTY. What D5 swaps in is the flush target and the event source; the
// desktop code under test never knows which session it runs against.
```

固定尺寸整屏、进程内事件队列、HostTable 方法调用。第八章裸机宿主换进来时，换的只是 flush 去哪、事件从哪来，受测的桌面代码一行不改。这条「桌面不知道自己跑在哪个会话上」的边界，就是从第一天这个替身的形状立起来的。

事件队列用 visor 的 RingQueue，容量常量旁边有一条算术注释：

```cpp
/// Total feed capacity in slots; the ring holds one fewer (one slot stays
/// empty to tell full from empty), so the default feed takes fifteen
/// events before it starts refusing.
static constexpr std::size_t kFeedCapacity = 16;
```

十六个槽只装得下十五个事件，空一格是为了区分满和空。替身不隐藏自己的极限：喂满了就拒收并计数，`dropped_event_count()` 把丢包报出来。测试替身先把自己的短板说清楚，判例才知道自己在信什么。

时钟也是脚本的。`advance_ms` 往前拨，无符号减法回绕是故意的，这条纪律 visor 教程讲过：时钟值会回绕，比较绝对值在回绕边界上就是错的。

## 最小桌面：一个普通的 Widget

桌面雏形简单到不像「桌面」：一块全屏底色，底边一行字。但它的身份在第一天就定对了，头文件注释写着：

```cpp
// The desktop is an ordinary Widget in visor's tree, so the platform rules
// are inherited, not reinvented: children draw in add order, hit testing
// walks them backwards, and a window added after the banner floats above
// everything added earlier.
```

桌面是树上的一颗普通控件，不加挂、串、收拢这些规则，全部继承平台的：子件按加入序画，命中测试倒着走，后加的窗口浮在先加的东西上面。桌面壳要做的只是往这棵树上加桌面语义（窗口框、图标、启动器），不去发明第二套树规则。

实现只有几行，底边一行横幅，注释把摆位理由也写了：

```cpp
// The banner rides the bottom edge, one font line tall with a margin
// on both sides; a bottom line reads as the desk's name plate and
// leaves the upper ground to the windows that arrive later.
```

`on_draw` 只做一件事，铺底色。无副作用、可重放，这条渲染纪律也是从第一行代码就开始守的。

## 旗舰判例：整帧离屏，逐像素读回

第一笔提交带两个测试文件，desktop_tests 里的旗舰判例是整帧判：

```cpp
FakeSessionHost host{480, 320};
Desktop desktop{kScreenBounds, "VESPER", vesper::fonts::kDemoFont, visor::ui::kLightTheme};
Frame frame;
visor::ui::render_window(desktop, frame, kScreenBounds, host.table());
VESPER_CHECK_EQ(host.flush_count(), 1);
VESPER_CHECK(host.regions()[0] == kScreenBounds);
```

先收到的证据是边界侧的：flush 恰好一次，区域恰好整屏。然后是像素侧的，注释原文：

```cpp
// D0 flagship judgment: one full frame off screen. The light theme keeps
// the ground unlit, so the banner is the only ink a correct frame may
// carry -- every lit pixel on the host must lie inside the banner's box,
// and the banner must have drawn at least one.
```

浅色主题下底色不点亮，所以整帧里唯一允许出现的墨就是横幅那行字。判例逐像素扫过整块 480x320，数横幅框内的点亮数与框外的点亮数，框外必须为零，框内必须大于零。期望不是拿另一份实现算出来的，是拿语义（「横幅是唯一的墨」）直接写死的，这就是字面断言。

测试断言宏也是自己的一小份，testing.hpp 里几十行：

```cpp
/// On a false condition, prints the expression text and counts. No
/// exceptions, no abort: a failed check never hides the ones after it.
#define VESPER_CHECK(cond) ...
```

失败不抛不崩，记一行、继续跑。一个判例挂了，同文件的其它判例照常执行，失败清单一次看全。

## 验收：一道闸，不是四条走廊

scripts/checks/acceptance.sh 把四道串成一条链：rel 构建与全部测试、ASan 加 UBSan 构建与全部测试、clang-format、clang-tidy。任一步失败脚本即停，不存在「跳过失败用例后报通过」的路径。这道闸从第一天就在，后面的每一章都以它全绿收尾。

X11 那条肉眼通道第一天生了口子但刻意不进验收：桌面最终要在没有 X11 的裸机上活，验收以离屏判例为准，肉眼只是开发时的方便。

## 本章验收

跟着跑到这一章时，先把一份 visor 的检出放在旁边，然后：

```bash
cmake -S . -B build -G Ninja -DVISOR_ROOT=/path/to/visor
cmake --build build
ctest --test-dir build --output-on-failure
scripts/checks/acceptance.sh
```

全绿后值得再做一件事：把 `VISOR_ROOT` 指向一个不存在的目录重新 configure，看那条 FATAL_ERROR 怎么报。错误路径也是接口，第一天就值得看一眼它长什么样。

下一章我们给桌面开第一扇窗：标题栏拖动、关闭键收回，还有拖动夹取的第一个教训。
