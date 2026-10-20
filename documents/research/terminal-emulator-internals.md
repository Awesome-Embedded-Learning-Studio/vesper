# 终端仿真器内幕：从 Konsole/VTE/alacritty/kitty 到 vesper 的 D3 与 D4

> 调研日期 2026-10-17 · 服务对象：vesper 手搓教学桌面 · 来源：网络检索加本地材料

本篇回答一个问题：vesper 的第一个 App「终端」到底在模拟什么。方法是把成熟终端的结构拆开看（第 1 节），把字节流的完整链路走一遍（第 2 节），用本地 PTY 实测 bash 与 vim 真正发来的字节定出必学子集（第 3 节），再看回滚、选择、字形这三块容易低估的领土（第 4、5 节），最后汇总成 D3/D4 的做与不做清单（第 6 节）。

本地实测材料（本机 WSL2 环境抓取，方法见文末说明）：`bash.raw`（bash `--noprofile --norc -i`，`PS1='$ '`，`TERM=xterm`，六条命令，去除探针注记后 279 字节）、`vim.raw`（`vim -u NONE` 编辑 40 行文本并退出，787 字节）、提示符下的 `stty -a` 快照、vim 内建 termcap 转储。

## 1. 四个样本：库与应用的分界，渲染的三条路线

| | Konsole | VTE (libvte) | alacritty | kitty |
|---|---|---|---|---|
| 形态 | 应用（可作 KPart 嵌入） | GTK 控件库 | 应用 | 应用 |
| 语言/界面栈 | C++/Qt QWidget | C++/GTK | Rust/winit | C 加 Python（Go 写 kittens） |
| 渲染 | QPainter 常规画法 | GTK/cairo | OpenGL（要求 OpenGL ES 2.0 以上） | OpenGL 加 SIMD 加线程化渲染 |
| 标签/分屏 | 都有，view 层功能 | 都没有，归宿主应用 | 都没有，留给 WM 或复用器 | 都有，内建布局系统 |
| 连字 | 支持 | 支持（ shaping 归 GTK 栈） | 不做（issue #50） | 支持（HarfBuzz 按行 shaping） |
| 双向文字 | 支持（TerminalDisplay 有 bidiMap/UBiDi） | 支持（src/bidi.cc） | 不做（issue #306） | 部分（复杂脚本有专门讨论） |

来源：Konsole 结构来自源码目录与头文件（[src 目录清单](https://api.github.com/repos/KDE/konsole/contents/src)、[TerminalDisplay.h](https://raw.githubusercontent.com/KDE/konsole/master/src/terminalDisplay/TerminalDisplay.h)），功能面来自[维基百科 Konsole 词条](https://en.wikipedia.org/wiki/Konsole)。VTE 定位来自[官方 README](https://gitlab.gnome.org/GNOME/vte/-/raw/master/README.md)「VTE provides a virtual terminal widget for GTK applications」与[维基百科 GNOME Terminal 词条](https://en.wikipedia.org/wiki/GNOME_Terminal)。alacritty 见[官方 README](https://raw.githubusercontent.com/alacritty/alacritty/master/README.md)。kitty 见[官网](https://sw.kovidgoyal.net/kitty/)与[overview 页](https://sw.kovidgoyal.net/kitty/overview/)。

### Konsole：分层的教科书

源码目录本身就是架构图：`src/session`（会话）、`src/terminalDisplay`（视图）、`src/Emulation` 加 `src/Vt102Emulation`（解析器，187.5 KB，全仓最大的文件）、`src/Screen` 加 `src/ScreenWindow`（网格模型与「视图窗口」）、`src/ptyqt`（PTY）、`src/history`（回滚）。头文件注释把关系说得很直白。Session.h 说「The pseudo-teletype (or PTY) handles I/O between the terminal process and Konsole」，且「Each Session can be connected to one or more views by using the addView() method」。TerminalDisplay.h 说「In terms of the model-view paradigm, the ScreenWindow is the model which is rendered by the TerminalDisplay」。

这给了一个直接可借的骨架：PTY、解析器、网格、视图四层分开，一个会话可挂多个视图（分屏共享同一屏幕内容的语义来源）。标签与分屏完全住在 view/框架层，Session 毫不知情。

### VTE：把「终端」做成控件

VTE 是库不是终端。gnome-terminal、Xfce Terminal、Terminator、Guake 这些应用的标签栏、菜单、配置界面都是应用自己的，VTE 只负责控件内的一块屏幕（pty 加仿真加渲染）。文本 rewrap（回滚内容随窗口宽度重排）由 Egmont Koblinger 实现、随 VTE 0.35.0（2013 年 11 月）落地，稳定用户拿到的是 2014 年 GNOME 3.12 的 VTE 0.36，跟踪条目 GNOME Bugzilla 336238，说明「回滚怎么存」也归控件管。教学价值在于边界画法：vesper 的 Terminal 控件应当长成 VTE 的形状（填进 WindowFrame 就能用），而不是长成 Konsole 整应用的形状。

### alacritty：集成优于重造

README 的立场句：「By integrating with other applications, rather than reimplementing their functionality, it manages to provide a flexible set of features with high performance」。FAQ 明说没有标签和分屏，因为它们「best left to a window manager or terminal multiplexer」。滚动回看靠 vi mode（Ctrl+Shift+Space 进入，键盘在回滚区里移动），外加搜索与 hints（正则高亮屏幕上的 URL 之类，见 [docs/features.md](https://raw.githubusercontent.com/alacritty/alacritty/master/docs/features.md)）。它对转义协议的支持反而很新：`extra/man/alacritty-escapes.7.scd` 列出 `CSI ? 2026 h/l`（同步输出）与 kitty keyboard protocol 的全套 `CSI u` 变体，OSC 52 剪贴板也支持（[escapes 手册](https://raw.githubusercontent.com/alacritty/alacritty/master/extra/man/alacritty-escapes.7.scd)）。

### kitty：把渲染当卖点，把协议当地盘

官网定位「The fast, feature-rich, GPU based terminal emulator」。overview 页的架构句：不依赖大型 UI 工具包，「using only OpenGL for rendering everything」，C 写性能敏感部分，Python 写易扩展的 UI 逻辑，Go 写命令行 kittens。它自己发明了一整层协议（图形协议、keyboard protocol、下划线着色），都被别的终端跟进（alacritty 的 escapes 手册就是证据）。连字与 emoji 走逐字形字体替换（overview 页）。

## 2. PTY 数据流全链路：一个按键的旅程

按 [The TTY demystified](https://www.linusakesson.net/programming/tty/) 的模型，vesper 终端里的完整链路是：

1. 按键到达终端进程（X11 下经 X server，fbdev 下经 evdev）。终端把它翻成字节（如 `a`，或方向键的 `ESC[D`，用不用应用光标模式由 DECCKM 决定）。
2. 终端把字节 `write` 进 PTY master fd。终端在这里只搬字节，不回显。
3. 内核 PTY 对的 slave 侧挂着行规程（N_TTY），按 slave 的 termios 处理：`ICANON` 时攒一行才交付并自带行编辑（退格、`^U` 杀行），`ECHO` 时把输入打回去，`ISIG` 时把 `^C` 变成 SIGINT 发给前台进程组（这两个字节永远不会到达应用的 `read`）。
4. 前台应用（bash）读到字节。输出路径反向：应用 `write` 到 slave，行规程的 `opost`/`onlcr` 把 `\n` 变成 `\r\n`，字节从 master 出来。
5. 终端从 master `read` 到字节，喂给转义序列解析器，更新网格，标脏，下一轮 `run_dirty` 画出去。

「谁在回显」取决于谁握着 termios。本地实测：bash 提示符下 `stty -a` 显示 `isig -icanon -echo`、`min = 1; time = 0`，即 readline 已把终端调成逐字符无回显模式，自己在管行编辑与回显（我敲的 `echo hello` 之所以出现在屏幕上，字节来自 readline 而非内核）。而 vim 运行时同样自己设 raw 模式。所以终端侧的实现律条只有一条：master 读到什么画什么，一个字节都不额外替用户回显。

窗口尺寸是另一半链路。行列数是内核里的两个变量，终端负责在 resize 时对 master 调 `TIOCSWINSZ`，内核更新 slave 的尺寸并向前台进程组发 SIGWINCH，编辑器类应用接信号后重画（The TTY demystified）。本地实测反向证明其必要性：探针没设过 winsize，`stty` 显示 `rows 0; columns 0`，vim 仍按 24x80 退化绘制。应用拿不到尺寸就用兜底值，但正常路径必须由终端 ioctl 喂进去。

进程怎么起来的：`forkpty()` 一步做完 openpty、fork、login_tty（建新会话、把 slave 设为控制终端、dup 到 stdio），父进程拿 master fd（[man7 forkpty(3)](https://man7.org/linux/man-pages/man3/forkpty.3.html)）。这正是旧仓 `linux_spawn`（forkpty 加 execvp）的形状，D4 直接抄。

## 3. 转义序列：必学子集与两个实测直方图

### 解析器：先立状态机，再填序列

[Paul Flo Williams 的 DEC 解析器状态机](https://vt100.net/emu/dec_ansi_parser)（vt100.net，2002 年发布）是事实上的参考方案：ground、escape、csi_entry/param/intermediate/ignore、osc_string、dcs_* 五组、sos_pm_apc_string，一张转移表覆盖「每个状态下的每个字节」，卖点是对残缺与恶意字节流也有定义好的恢复路径（CAN/SUB 从任何状态取消并回 ground，ESC 既开新序列也终止控制串）。它还有公有领域 C 实现 vtparse。对 vesper 的意义：D3 的第一天就照这张状态机写定容解析器，而不是边写序列边 if-else 堆山。实测里 vim 发的 `ESC[?4m`（见下文）就是「未知私有序列必须静默吞掉」的活教材。

### bash 实测（279 字节）

| 字节/序列 | 次数 | 来源与含义 |
|---|---|---|
| `CR`/`LF`（合成 `\r\n`） | 16/10 | 行规程 onlcr 翻译，正文的换行 |
| `ESC[?2004h` / `ESC[?2004l` | 各 3 | readline 开关 bracketed paste，每条命令一对 |
| `ESC[0m`、`ESC[01;32m`、`ESC[37;41m` | 共 5 | `ls --color` 的 SGR：复位、粗体绿（可执行）、白底红字（setuid） |
| `ESC[H`、`ESC[2J`、`ESC[3J` | 各 1 | `clear`：光标归位、清屏、清回滚（ED 3 是 xterm 扩展） |
| `TAB` | 1 | 制表位，终端自己移动光标 |

结论：bash 日常回显阶段只需要「打印、CR/LF、TAB、SGR、清屏归位」五件事。默认 PS1 不带颜色时提示符更是纯文本。（发行版默认 PS1 带 SGR 颜色，序列量略增，种类不变。）

### vim 实测（787 字节，`-u NONE`，TERM=xterm）

| 字节/序列 | 次数 | 含义 |
|---|---|---|
| `ESC[?1049h` / `ESC[?1049l` | 各 1 | 进出备用屏幕 |
| `ESC[22;0;0t` / `ESC[23;0;0t` | 各 2 | xterm 窗口标题栈的保存/恢复 |
| `ESC[>4;2m` / `ESC[>4;m` | 3/2 | modifyOtherKeys 开关（vim 的 t_TI/t_TE） |
| `ESC[?4m` | 2 | t_RK「request terminal keyboard protocol state」（本机 vim92 doc/term.txt 第 478 行），多数终端直接忽略 |
| `ESC[?1h` + `ESC=` / `ESC[?1l` + `ESC>` | 各 1 | DECCKM 应用光标键加应用键盘模式 |
| `ESC[?2004h/l` | 4/3 | bracketed paste |
| `ESC[1;24r` | 1 | DECSTBM 把滚动区设为全屏 1 到 24 行 |
| `ESC[?12h` / `ESC[?12l` | 1/2 | 光标闪烁开/关（t_vs/t_VS） |
| `ESC[?25l/h` | 多次 | 画面前藏光标、画完再显示 |
| `ESC[H`、`ESC[2J` | 各 1 | 开场归位加清屏 |
| `ESC[row;colH` | 8 | CUP 定位（状态行、逐行重画、光标回位） |
| `ESC[K` | 2 | 清到行尾（状态行刷新） |
| `ESC[27m` `ESC[23m` `ESC[29m` `ESC[m` | 各 1 | 开场逐项复位斜体/粗体/删除线再总复位 |
| `BEL`、`BS` | 2/2 | 边缘移动到头时响铃、退格 |

vim 的一次完整会话只用 15 类序列。其中重量级概念三个：备用屏幕（1049，进 vim 时整屏内容存走、退出还原，vim 内容不进回滚历史）、滚动区（DECSTBM，全屏设区是开场白，编辑器分屏时才会设子区域）、绝对定位（CUP，屏幕即帧缓冲，每帧重画）。DECSTBM 语义按 [VT102 用户手册第 5 章](https://vt100.net/docs/vt102-ug/chapter5.html)：区域最小两行，设区后光标归位，IND/RI 只在区域内滚，区外行纹丝不动。SGR 参数语义（30-37/90-97、`38;5;N`、`38;2;R;G;B`、39/49）与 ED/EL 各变体见[维基百科 ANSI escape code](https://en.wikipedia.org/wiki/ANSI_escape_code)。

顺带一条本地佐证：`infocmp xterm-color` 的 `smcup`/`rmcup` 是 `ESC7`/`ESC8` 加 `ESC[?47h/l` 的旧式备用屏幕，1049 是后来把存光标、切屏、清屏三合一的版本（维基百科同条目）。新旧两代并存，D4 只认 1049 与 47，都做即可，成本相同。

### 显式不学（D3/D4 阶段）

OSC 全家（标题 0/2 可选做，52 剪贴板不做）、SIXEL 与图形协议、DECRQSS 查询的应答（vim 发 `ESC P $q q q ESC \` 查光标样式，不答则 vim 用默认样式，无碍）、kitty keyboard protocol、charset 切换（`ESC ( 0` 画线字符集）、鼠标报告（`?1000` 系，bash 与 vim 默认都不开）。原则：认不出的一律按状态机静默吞到序列末尾。

## 4. 回滚缓冲与选择复制

### 回滚的三种存法

- Konsole 的 `src/history` 由几个小类组成，`HistoryScrollNone` 不存、`HistoryScrollFile` 落到临时文件、`HistoryType` 工厂配 `compact/` 子目录的紧凑内存块（[目录清单](https://invent.kde.org/api/v4/projects/utilities%2Fkonsole/repository/tree?path=src/history&ref=master)）。按策略可换。
- alacritty：环形缓冲，配置项 `history` 默认 10000 行、上限 100000、设 0 关闭（[alacritty.5 手册](https://raw.githubusercontent.com/alacritty/alacritty/master/extra/man/alacritty.5.scd)）。
- kitty：`scrollback_lines` 默认 2000 行存内存，负数为无限，回看用 Ctrl+Shift+H 把缓冲喂给分页器（默认 less）当普通文本翻（[kitty.conf 文档](https://sw.kovidgoyal.net/kitty/conf/)）。等于把「回滚浏览」外包给了分页器。

vesper D3/D4 的合适量级：内存里一条按行存的环形队列（visor 已有 RingQueue 词汇），固定容量，先不做 reflow（宽度变化时旧行保持原折行，这是 VTE 0.35（2013 年）之前全世界的默认行为，教学上完全站得住）。

两条必须对的语义。一，备用屏幕期间的内容不进回滚，否则退出 vim 后往上翻会翻到编辑器残影。二，回滚行按「逻辑行」存，一次折行算一行还是多行影响选择与搜索，alacritty 的 vi mode 按逻辑行移动光标。

### 选择与复制

X11 的世界有两个独立的「剪贴板」：PRIMARY 与 CLIPBOARD。选中即占有 PRIMARY，中键粘贴 PRIMARY，Ctrl+C/V 走 CLIPBOARD，X server 只做中介不存数据（[movq.de 的 selection 科普](https://movq.de/blog/postings/2017-04-02/0/POSTING-en.html)）。终端的祖传惯例：鼠标拖选即写 PRIMARY，不碰 CLIPBOARD。alacritty 的默认值印证：`save_to_clipboard = false`，另有 `semantic_escape_chars` 定义双击「词选择」的分界符集（逗号、竖线、引号、各类括号、制表符等），三击选整行，按住 Ctrl 进块选择（[alacritty.5 手册](https://raw.githubusercontent.com/alacritty/alacritty/master/extra/man/alacritty.5.scd) 与 [features.md](https://raw.githubusercontent.com/alacritty/alacritty/master/docs/features.md)）。vesper 的取舍：PRIMARY 语义（拖选写入加中键粘贴）随 D4 终端接线做，CLIPBOARD 与拖放归任务栏或 WM 层。

粘贴侧的坑是 bracketed paste 存在的理由：往 vim 里粘一坨文本时，换行与缩进若被当成敲键会触发自动缩进连环爆炸，`ESC[?2004h` 之后终端把粘贴内容包在 `ESC[200~` 与 `ESC[201~` 里发，应用就能区分「人敲的」与「贴的」（维基百科 ANSI escape code 条目）。D4 应做这一对，代价是两行代码加一个状态位。

## 5. 网格与字形：cell、宽字符、双向文字、连字

### cell 模型

所有主流终端的屏幕本体都是定长 cell 数组：Konsole 的 `Character *_image`（TerminalDisplay.h），VTE 的 `cell.hh`/`attr.hh`，alacritty 同构。一个 cell 装码点（或码点序列）、前景背景属性、标志位。定长是性能根基：定位 O(1)、拷贝一条 memmove、脏区按 cell 计。vesper 的 D3 从第一天就该是 `Cell { uint32_t cp; uint8 attr; }` 而不是字符串数组。

### 宽字符

[Markus Kuhn 的 wcwidth](https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c) 是事实标准实现：East Asian Wide/Fullwidth（TR11 的 W/F 类）占两格，组合符号（Mn/Me）零格，歧义类（A）默认一格。占两格意味着 cell 要么存「宽字符加空白占位」，要么按两格跨位查询，光标移动、CUP、EL 都要按显示宽度算。D3 的 80x25 起步可以直接只做一格字符加 wcwidth 表骨架，中文宽字符排到 D 后期，但要现在就把「列号按显示宽度」写进代码而不是按 cell 下标。

### 双向文字

真实分布：Konsole（ICU 的 UBiDi，bidiMap）与 VTE（bidi.cc）做了，alacritty 没做（阿拉伯/希伯来显示为方块或乱序，issue #306 从 2017 开到现在）。这块连图形协议时代的 GPU 终端都普遍缺席（Warp 同样没做）。教学结论：vesper 显式不做，文档写明，这本身就是「GUI 与文本渲染的距离」的教材。

### 连字

alacritty 的拒绝理由一句顶一万句，issue #50：「Implementing it isn't the problem, doing it properly is」。把 `==` 合并成一个两格宽的等号字形不难，难的是它与 cell 网格的每一处交互（选择半个连字怎么办、光标停在连字中间怎么办、半格重画怎么办）。kitty 选择做全套：每行文本走 HarfBuzz 整行 shaping（[Thottingal 关于终端复杂脚本的博客](https://thottingal.in/blog/2026/03/22/complex-scripts-in-terminal)），代价是网格模型处处打补丁。vesper 站 alacritty 这边：不做连字，做等宽位图字形。

### Mono1 的天然对齐

vesper 第一版只有 1bpp 黑白，这不是缺陷，是路线：单色终端时代 SGR 颜色本来就映射成属性。粗体（SGR 1）、下划线（4）、反白（7）三个属性位就够把 ls 的彩色分类保留成视觉差异。D3 阶段 `ESC[01;32m` 可以直接映成「反白」，`ESC[37;41m` 映成「下划线加反白」，等 RGB565 到位再放开调色板。旧仓 terminal.cpp 的 16 色与 256 色查找表是现成参考（`~/Cinux-GUI/core/widget/terminal.cpp`，只读参考不搬代码）。

## 6. 映射到 vesper D3 与 D4

### D3（80x25 网格加假源）必做

- `Cell{cp, attr}` 定容网格加光标（行列、可见性、闪烁位）。
- 按 Paul Flo Williams 状态机写的定容解析器，states 与 transitions 照抄该文档，只实现下表 action。
- 必做序列全集（bash 与 vim 实测的并集，恰好就是最小完备集）：

| 类别 | 序列 |
|---|---|
| C0 | `CR`、`LF`（LF 在底行触发滚屏）、`TAB`、`BS`、`BEL`（先做无声） |
| ESC | `ESC 7`/`ESC 8`（存/复光标）、`ESC D`/`ESC M`（区内下/上滚）、`ESC =`/`ESC >`（吞掉即可）、`ESC c`（复位，选做） |
| 光标 | `CUU/CUD/CUF/CUB`（`ABCD`）、`CUP`（`H` 与 `f`）、光标显隐 `?25` |
| 擦除 | `ED`（`J`，含 3）、`EL`（`K`） |
| 属性 | `SGR`（`m`：0/1/4/7/22/24/27 加颜色参数按 Mono1 映射，未认识的 SGR 参数跳过） |
| 区域 | `DECSTBM`（`r`，含归位语义）、IND/RI 只滚区内 |
| 屏幕 | 备用屏幕 `?1049`（含保存主屏与光标）、bracketed paste `?2004`（开关各吞一对） |
| 未知 | 任何认不出的按状态机吞到 final byte，不画不响 |

- 折行：80 列写满后悬置（deferred wrap：光标停在末列，下一可打印字符才换行，即 xenl 语义），这是兼容性高频错点，vim 对窄屏的长行依赖它。
- 假源判例直接用本次抓的真实字节流：`bash.raw` 与 `vim.raw` 去掉 `<<<SENT>>>` 注记后就是两份 fixture，喂进离屏 Terminal，断言见下。

### D4（真 PTY 加 bash）必做

- `linux_spawn`（forkpty 加 execvp）拉 bash，master fd 设非阻塞。
- 事件循环按交接文档 D4 切法：poll 编排宿主事件源加各终端 master fd，谁有货醒谁，队列空则睡。master 读到 EAGAIN 不算错误。
- 启动后立即 `TIOCSWINSZ` 设 80x25（本地实证：不设则内核记 0x0，应用退化 24x80，resize 路径从此不通）。WindowFrame 拖大后重设并发 SIGWINCH。
- 按键翻译：可打印键走码点直接写 master，Enter 写 `\r`，Backspace 写 `DEL`（127），方向键按 DECCKM 当前值发 `ESC[A`，或把前缀 `ESC [` 换成 `ESC O`（即 `ESC O A`）。
- 选择与粘贴：拖选写入 X11 PRIMARY，中键粘贴 PRIMARY（第 4 节口径）；CLIPBOARD 与拖放不做，归任务栏或 WM 层。

### 显式不做（写进文档与代码注释）

标签与分屏（vesper 的 WM 与窗口层负责，与 alacritty「留给窗口管理器」同一哲学，也正好给 D6 的桌面愿景留活）、GPU 渲染（Mono1 下无意义）、连字、双向文字、256 色/真彩色（存 attr 位，出图等 RGB565）、OSC 52、鼠标报告、DECRQSS 应答、reflow、CLIPBOARD 与拖放（归任务栏或 WM 层）。

### 验收判例建议

| 判例 | 输入 | 断言 |
|---|---|---|
| 回放 bash | 喂 `bash.raw` 全量 | 屏上出现 `$ `、`hello`、`/bin/ls` 与 `/bin/mount` 两色块（Mono1 下成两种 attr），`clear` 之后整屏空白且光标在 1 行 1 列 |
| 回放 vim 进出 | 喂 `vim.raw` 到 `:q!` 前 | 底行在 24 行，正文 23 行对齐，光标可寻址到 1 行 1 列，滚屏后主屏内容原样待恢复 |
| 备用屏幕还原 | 先打印标记串再回放 `vim.raw` 整段 | 退出 1049 后网格恢复标记串，回滚队列里没有 vim 正文 |
| 滚动区隔离 | 手工注入：设 `ESC[5;10r` 后连发 20 个 `LF` | 只有 5 到 10 行在滚，1 到 4 行与 11 行以下不变 |
| SGR 只动属性 | 打印 `X` 后发 `ESC[1m` 再打印 `X` | 第二个 X 反白（attr 变），第一个 X 不变 |
| 悬置折行 | 打印 81 个 `A` 加换行 | 第 81 个 A 在第 2 行第 1 列，两行都不产生多余滚动 |
| 未知序列健壮性 | 注入 `ESC[?4m`、`ESC[999;999H`、截断的 `ESC[38` 后跟 NUL 与随机字节流 | 不死循环不越界，后续可打印字符照常落格 |
| PTY 回环 | 真 bash 下 `printf 'hi\n'` | 渲染帧含 `hi`，`^C` 后提示符刷新（ISIG 在内核侧，终端无感知） |

## 对 vesper 的启示

- 概念参考 Konsole 的 Session/ScreenWindow/TerminalDisplay 三层：会话（PTY 加仿真加网格）与视图分离，一个会话可挂多个视图，这是将来桌面分屏共享终端的语义底座，也是 D6 进程化的天然切面。
- 概念参考 VTE 的「终端做成控件」边界：vesper 的 Terminal 是填进 WindowFrame 的控件，标签分屏归 WM 层，App 口只报生命周期。
- 概念参考 alacritty 的集成优于重造哲学：回滚浏览可外借（kitty 外包给分页器），搜索与 hints 属于 viewport 层，都不必进 D3/D4。
- 概念参考 xterm 的 bracketed paste：两行状态换 vim 粘贴不出错，D4 值得带。
- 概念参考 VT102 的 DECSTBM 与备用屏幕：编辑器类 App 与终端的全部契约就是这几个 mode，D3 把它们做对，vim 就能跑。
- 概念参考 vt100.net 的 DEC 解析器状态机：D3 第一天照表实现，健壮性是设计出来的，不是补出来的。
- 概念参考 Kuhn wcwidth 的宽度分类法：列号从第一天就按显示宽度计，给中文留轨。
- 单色不是过渡态：SGR 属性位（粗体/下划线/反白）在 Mono1 上就是 ls 颜色的合法呈现，与 visor 的单色身份一致。

## 参考来源

- https://www.linusakesson.net/programming/tty/ — 支撑第 2 节全部链路描述（行规程、termios、ECHO/ICANON/ISIG、opost、TIOCSWINSZ 与 SIGWINCH、PTY 主从结构）。
- https://man7.org/linux/man-pages/man3/forkpty.3.html — 支撑第 2 节 forkpty/openpty/login_tty 语义。
- https://api.github.com/repos/KDE/konsole/contents/src 与 https://invent.kde.org/api/v4/projects/utilities%2Fkonsole/repository/tree?path=src/history&ref=master — 支撑第 1 节 Konsole 目录结构与第 4 节 history 类族。
- https://invent.kde.org/utilities/konsole/-/raw/master/src/session/Session.h — 支撑第 1 节 Session/PTY/views 关系引文。
- https://raw.githubusercontent.com/KDE/konsole/master/src/terminalDisplay/TerminalDisplay.h — 支撑第 1 节 model-view 引文、QWidget 形态、bidiMap，及第 5 节 `Character *_image`。
- https://en.wikipedia.org/wiki/Konsole — 支撑第 1 节 Konsole 功能面（标签、分屏、bidi）。
- https://gitlab.gnome.org/GNOME/vte/-/raw/master/README.md 与 https://gitlab.gnome.org/api/v4/projects/GNOME%2Fvte/repository/tree?path=src&ref=master — 支撑第 1 节 VTE 定位与 cell/chunk/bidi 文件布局。
- https://en.wikipedia.org/wiki/GNOME_Terminal — 支撑第 1 节 VTE 消费者名单与「GNOME Terminal 3.12（VTE 0.35）起支持 rewrap」表述。
- https://gitlab.gnome.org/GNOME/vte/-/raw/0.35.1/NEWS 与 https://bugzilla.gnome.org/show_bug.cgi?id=336238 — 支撑第 1 节 rewrap 的实现者与版本：NEWS 的 0.35.0 小节「Rewrap contents upon window resize (#336238)」、0.35.1 小节「Add API to enable/disable rewrap on resize (#336238)」，Bugzilla 336238 为跟踪条目，与仓库 git 历史提交 01380dae（Egmont Koblinger，2013-11-18，含于 tag 0.35.0）互相印证，原存疑撤销。
- https://raw.githubusercontent.com/alacritty/alacritty/master/README.md — 支撑第 1 节 alacritty 定位、GPU、OpenGL ES 2.0 门槛、不做 tabs/splits 的原话。
- https://raw.githubusercontent.com/alacritty/alacritty/master/docs/features.md — 支撑第 4 节选择语义（双击/三击/Ctrl 块选）。
- https://raw.githubusercontent.com/alacritty/alacritty/master/extra/man/alacritty-escapes.7.scd — 支撑第 1 节协议支持面（2026 同步输出、kitty keyboard protocol、OSC 52）与第 3 节「私有 mode 1/25/1049/2004 在支持列表」。
- https://raw.githubusercontent.com/alacritty/alacritty/master/extra/man/alacritty.5.scd — 支撑第 4 节 history 默认 10000 上限 100000、semantic_escape_chars、save_to_clipboard 默认值。
- https://github.com/alacritty/alacritty/issues/50 与 https://github.com/alacritty/alacritty/issues/306 — 支撑第 5 节连字拒绝原话与 bidi 缺席证据。
- https://sw.kovidgoyal.net/kitty/ 与 https://sw.kovidgoyal.net/kitty/overview/ — 支撑第 1 节 kitty 架构句（仅 OpenGL、C/Python/Go、SIMD、连字与 emoji 逐字形替换）。
- https://sw.kovidgoyal.net/kitty/conf/ — 支撑第 4 节 scrollback_lines 默认 2000、负数为无限、分页器默认 less。
- https://thottingal.in/blog/2026/03/22/complex-scripts-in-terminal — 支撑第 5 节 kitty 逐行 HarfBuzz shaping 的描述。
- https://vt100.net/emu/dec_ansi_parser — 支撑第 3 节状态机各状态、转移表设计目标、CAN/SUB/C1 行为与 vtparse。
- https://vt100.net/docs/vt102-ug/chapter5.html — 支撑第 3 节 DECSTBM/DECOM/IND/RI/CUP/DECSC/DECRC 语义。
- https://en.wikipedia.org/wiki/ANSI_escape_code — 支撑第 3 节 CSI 结构、SGR 参数族、ED/EL 变体、1049 与 2004 语义，及第 4 节 bracketed paste 存在理由。
- https://www.cl.cam.ac.uk/~mgk25/ucs/wcwidth.c — 支撑第 5 节宽度分类（W/F 两格、Mn/Me 零格、歧义一格）。TR11 原文 https://www.unicode.org/reports/tr11/ 未单独核对，随 Kuhn 实现转引。
- https://movq.de/blog/postings/2017-04-02/0/POSTING-en.html — 支撑第 4 节 PRIMARY/CLIPBOARD 双通道与 selection owner 机制。
- 本地材料（无 URL，方法记录备查）：`bash.raw`/`vim.raw` 来自 /tmp/term_probe/ 下的 pty.fork 探针（2026-10-17 抓取，本文所有直方图与序列表的原始出处）。提示符 termios 快照来自对 bash 会话 tty 设备的 `stty -a`。vim t_ 选项值来自 vim 全屏会话内 `:set termcap` 重定向转储，t_RK 语义查自本机 /usr/share/vim/vim92/doc/term.txt。smcup/rmcup 旧式备用屏幕来自本机 `infocmp xterm-color`。旧仓设计底稿 Cinux-GUI 的 core/widget/terminal.cpp（478 行）支撑第 5 节调色板参考一说。
