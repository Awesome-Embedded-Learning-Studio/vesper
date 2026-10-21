# fbdev 裸机通道：工具、口径与排障沉淀

> D5 留下的运维知识，2026-10-20 成文。范围是 `scripts/vm/` 那台 QEMU 裸机的构建缓存、三种跑法、取证口径，和调试期实际咬过人的地方。判例现状：串口加像素门 PASS，`INPUT=1` 的真 VNC 交互判例 PASS（双击、拖拽折返、松手稳定、shell 执行回显、关窗六关），作者手动场复核可以。

## 构建与缓存

内核与 busybox 编一次缓存着，日常重拉只需后两步（桌面源码变了才需要重编静态件）：

```bash
scripts/vm/build_kernel.sh     # 首次：拉 6.18.57 定版 tarball 自编，--http1.1（HTTP/2 会断流）
scripts/vm/build_busybox.sh    # 首次：静态 busybox，源自动拉 1.37.0 定版进 third_party/，BUSYBOX_SRC 可覆盖
scripts/vm/build_static.sh     # 桌面静态件（改了 C++ 之后跑这个）
scripts/vm/build_initramfs.sh  # 打包 rootfs；FBDEV_TIMEOUT 定场时，VESPER_FBDEV_TRACE=1 开串口跟踪
```

`build_static.sh` 走 `third_party/visor` 子模块（锚点见 CLAUDE.md 借壳红线的 tested-against）。内核与 busybox 的钉是定版号加 SHA-256 校验和，写死在各自脚本里，拉下来验不过就大声失败：验证流对任何人可复现，同一字节进、同一判例出。

## 三种跑法

```bash
scripts/vm/smoke.sh                  # 判例道：串口标记 + 截屏像素门，headless
INPUT=1 scripts/vm/smoke.sh          # 交互判例道：input_gate.py 自己当 VNC 客户端驱动键鼠，六关像素断言
VNC=1 scripts/vm/smoke.sh            # 肉眼道：VNC 服务在 127.0.0.1:5999，人上
```

手动长场的 QEMU 长命令在 `smoke.sh` 里照抄即可（KVM 直通、vga std、qemu-xhci 加 usb-tablet、串口到文件、监视器 unix socket）。要点两条：`FBDEV_TIMEOUT` 给足（默认 20 秒是判例道的节拍，手动场给 3600）；跑之前确认 `/dev/kvm` 属组是 `kvm`（WSL 重启可能把它重建成 root:kmem，症状 EACCES，`sudo chgrp kvm /dev/kvm` 修）。

## 取证口径

- **串口跟踪**：打包时 `VESPER_FBDEV_TRACE=1`。`trace[N] keydown|pdown|pup|move|wheel ...` 是翻译层出口的事件（带键码或桌面坐标），`trace[N] poll=... kbd=... ptr=...` 是主循环唤醒源，`trace[N] beat` 约五秒一次心跳证明循环活着。冻结时心跳还在而事件行停了 → 事件没到桌面；心跳也停了 → 主循环卡死。
- **VNC 连接真实性**：QEMU 监视器 `info vnc`，有 `Client:` 行才是真连着。viewer 断线后画面冻在最后一帧，操作打进空气，这是排障期浪费过两轮的假线索。
- **截屏**：`printf 'screendump /tmp/s.ppm\n' | socat - UNIX-CONNECT:<run>/monitor.sock`，PPM 用 `shot_gate.py` 的读法或 PIL 分析。
- **坐标口径**：guest fb 实际 1280x800（bochs-drm 首选模式，`video=` 参数拗不过它），桌面 640x480 落左上，fb 像素与桌面像素 1:1；tablet 轴程 0..32767 映射整个 fb，翻译层按 EVIOCGABS 实测轴程折算成桌面像素差分。

## 咬过人的地方（症状 → 病因 → 解）

- **PTY 全灭（forkpty ENOENT）**：devtmpfs 挂载盖掉 initramfs 静态 /dev，把里面的 pts 目录一起藏了，devpts 挂了个空目标。解：/init 挂完 devtmpfs 补 `mkdir -p /dev/pts` 再挂 devpts。
- **指针劈叉路由**：QEMU 对没做 SetEncodings 握手的 VNC 客户端，按键事件走 usb-tablet、移动事件按相对路由进 PS/2 鼠标（guest 的 event1，桌面不读它），表象是「按了没动、像鼠标死了」。正经 viewer（TigerVNC 等）握手都会带这一步；程序化驱动必须自己补（`input_gate.py` 的 `connect_vnc` 有注释）。
- **HMP 驱动键鼠不通**：监视器 `mouse_move` 是相对事件，只进 PS/2；`sendkey` 命令执行但 guest event0 上看不到事件。结论：程序化驱动输入走 RFB，别走 HMP；HMP 只用来截屏和查状态。
- **场时耗尽的遗容**：/init 的 `timeout N` 到点 SIGTERM 收走桌面（串口 `GUEST_RUN_RC=143`），帧冻在最后一屏，远看跟活着没区别。排查前先看串口有没有 RUN_DONE。
- **内核下载断流**：cdn.kernel.org 的 HTTP/2 会被重置，脚本已固定 `--http1.1`。

## 未了

VM1 时代有一次「双击开窗后指针全哑」的观察，当时无跟踪、后续未再复现；现行交互判例已覆盖同一序列（含拖出界折返与松手稳定）。若复发，按上面的取证口径抓现场：先分清事件没到桌面还是主循环卡死，再看 InputRouter 捕获是否在双击序列后未释放（visor `49f95c0` 的手势层与 press 所有权的交互是首查点）。
