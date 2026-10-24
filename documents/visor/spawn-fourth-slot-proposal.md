# visor stage-4 提案底稿：宿主表第四口 spawn

> 起草日 2026-10-24 · 状态：已并入上游（visor main，PR #3，2026-10-24；落地时 fd:N=path 暗语改为显式 gifts 表，白名单关 fd 用 fork+execve 而非 posix_spawn） · 依据：vesper C 系列（合成器主线，documents/desktop-roadmap.md）全程实现经验,本仓分支 compositor-c-series

## 0. 这份文档是什么

给 visor 会话的上游提案底稿,对应 visor stage-4 挂起议题「宿主表要不要第四口 spawn」。vesper 的 D6a(私有通道拉起最小子程序)与 C 系列(XWayland 拉起、宾客成窗)已经把 spawn 的形状在消费侧跑熟,这份底稿把经验提炼成 visor 侧的接口提案。Cinux 侧将来的 fork/exec 对接设计吃同一份底稿。

## 1. spawn 在 C 系列里实际干了什么(提案的经验底座)

- **D6a**:posix_spawn 把子程序的 stdio 变成私有通道(socketpair 一端 dup2 到 0 加 1),子程序零依赖、零协议库,vesper.session(wire 加 child_spawn)单管双端。
- **C5(XWayland 桥)**:同一姿势拉起一个真进程(Xwayland),但它需要三份陪嫁:一条 Wayland 客户端连接(socketpair 经 WAYLAND_SOCKET 递 fd)、一条 wm 连接(经 -wm 参数递 fd)、一个 displayfd 管道读回显示号。fork 加 execl 手工搭的这些腿,正是 spawn 口该收编的形状。
- **C6(将来 Cinux)**:fork/exec 在 Cinux 侧同形,填表人换成 Cinux 的进程系统。

结论:spawn 口不是「拉起一个程序」一个字,是「拉起一个程序,并把它需要的环境说清楚」。环境里最难的部分就是递 fd。

## 2. 提案:宿主表第四口

HostTable 现有三口(flush、poll_event、clock_ms),提案加第四口:

```c
/// Spawns one program with the host's wiring. Returns the pid (and the
/// caller-owned channel fd) or -1 with the cleanup already done.
/// env carries "KEY=VALUE" strings; any fd entries ride as
/// "fd:N=path" -- the host dups N into the child at the named spot.
int (*spawn)(void* context, const SpawnRequest* request) noexcept;
```

SpawnRequest 的字段按消费侧实测收敛:

- `path`、`argv`:execl 语义,不变。
- `env`:字符串数组,KEY=VALUE。WAYLAND_DISPLAY 一类由填表人拼好,visor 不解释。
- `fd_gifts`:「fd 号 = 谁持有」的表。D6a 的形状是「socketpair 一端 dup2 到子程序 0 加 1」;C5 的形状是「三个 fd 原样递进子程序,编号由参数自己带」。两种都已实测,第四口至少要容得下「原样递 fd」这一种(dup2 到 stdio 是它的特例)。
- 收尾:子程序退出后的 reap 归填表人(waitpid),spawn 口只负责生,不负责收——与 D6a 现行分工一致。

## 3. 为什么是宿主表的一口,不是 vesper 私有的工具函数

- 三级宿主梯队(fake、X11/fbdev、Cinux)对 spawn 的实现不同:fake 侧根本没有真进程,判例里第四口可以返回「不支持」;Linux 侧 posix_spawn;Cinux 侧将来 fork/exec。表格化是 visor 对「环境差异」的一贯答案。
- spawn 的语义跟着宿主走(WSL 的 Xwayland 路径与 Cinux 现场的程序加载器路径不同),塞进核心库会把核心绑死在一族操作系统上,违反 base 无操作系统可用的约束。

## 4. 边界与不做的事

- 不做进程监督(退出回调、重启):填表人 poll 自己的通道就知道死活,D6a 的 child_gone 一线已够。
- 不做环境变量解释(如自动注入 DISPLAY):拼环境是填表人的知识,visor 只递字符串。
- 不做 fd 之外的 IPC 发明:socketpair、pipe、pty 都是现成系统形状,第四口只递。

## 5. 判据

- visor 侧:示例改用第四口拉起一个双进程样例(借 D6a 的 wire),fake 宿主返回不支持时样例跳过。
- vesper 侧(回填后):child_spawn 与 XWayland::launch 的 fork 加 execl 段换成第四口,判例全绿,行为零变化。
- Cinux 侧(将来):站 26 现场用同一口拉 Cinux 用户程序上桌。

## 6. 时机

visor 侧无前置依赖,任意时点可做。vesper 侧回填在 C 系列收官、作者过目分支之后。
