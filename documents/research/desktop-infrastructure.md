# 桌面地基调研：窗口与控件之外的 Linux 桌面基建

> 调研日期 2026-10-17 · 背景：手搓教学桌面项目的立项调研 · 来源：网络检索加本地材料

## 0 这份文档看什么

KDE 与 GNOME 这样的成熟桌面，窗口系统和控件库之外还压着一整层与 GUI 无关的基建：进程间通信总线、用户级服务管理、音频服务器、一套 XDG 文件约定、文本渲染三件套、剪贴板协议、输入法框架、权限中介。平时没人看见这层，拿掉任何一件，桌面都会缺一块日常功能。

本篇逐项过八个题目，每项回答三问：它解决什么问题，没有它桌面能不能活，一个自研的小桌面该取、该舍还是该假装。假装指的是做出接口形状而不做真实现，教学项目里这是常见的手法：概念先立，实现后补。

贯穿全篇有一个观察：八件里六件的存在理由是「多个进程要协作」。总线因为进程要说话，服务激活因为进程要懒启动，portal 因为进程不可信，PipeWire 因为进程要抢同一个声卡。单进程起步把这层整体推迟，但立起进程边界之后，每一件都会以某个接口形状回来。这份调研就是提前把每个「回来的形状」认出来。

另一面，这层基建自己也离 GUI 很远。PipeWire 不关心窗口，fontconfig 不关心控件，trash 规范只是一份目录约定。「GUI 离操作系统很远」这条教学论据在本篇再进一层：GUI 甚至离桌面地基都很远，而桌面地基离 GUI 也很远。

## 1 D-Bus：会话总线与服务激活

D-Bus 是 freedesktop.org 上的进程间消息系统，2002 年由 Red Hat 的 Havoc Pennington、Alexander Larsson、Anders Carlsson 起头，设计明显受了 KDE 此前 DCOP 的影响。规范首版修订 0.8 发布于 2003-09-06，协议自 2006-11-08 冻结为只做兼容扩展，现行文本是 0.43（2024-10-29），1.0 定稿在 2006 年 11 月（来源：[D-Bus 规范](https://dbus.freedesktop.org/doc/dbus-specification.html)、[Wikipedia D-Bus](https://en.wikipedia.org/wiki/D-Bus)）。它在 GNOME 侧逐步顶替了 CORBA 系的 Bonobo，KDE 4 起换掉自己的 DCOP，两家桌面从此共用一条总线。

模型三句话说得完。每个进程连到一个 bus daemon，连上即得唯一名（如 `:1.42`），还可持有可读性好的知名名（如 `org.freedesktop.NetworkManager`）。消息分三种：method call（要回执的调用）、method return 与 error（回执）、signal（广播，不回执）。目的地址是总线名加对象路径加接口，形状像地址加端口加方法名。

桌面一般跑两条总线。system bus 全机一条，NetworkManager、BlueZ、logind 这类系统服务挂在这里。session bus 每个登录会话一条，桌面自己的件挂在这里：通知（org.freedesktop.Notifications）、媒体播放器遥控（MPRIS）、系统托盘（StatusNotifierItem）、电源管理、输入法、第 8 节的 portal，全走它。会话总线还负责激活环境的继承：被激活的服务拿到的是总线守护进程的环境，所以显示相关变量要专门灌进去（规范原文与 `UpdateActivationEnvironment` 接口即为此设）。

服务激活是这层里最值得抄的概念。消息发向一个没人持有的知名名时，总线按 `/usr/share/dbus-1/services` 下的 `.service` 文件找出提供者，拉起进程，等它拿到名字，再把最初那条消息递过去。规范明说消息会 held 到激活成功或失败为止，失败才回错。发件人完全不知道对面的进程是刚被拉起来的，桌面各件因此不必常驻，点名即到。`.service` 文件里的 `SystemdService` 键还允许把激活转交给 systemd 用户实例（2006 年后的修订加入，见规范）。

同一协议后来有了新实现，也出现过失败的替代方向。systemd 2013 年重写出 sd-bus，API 随 systemd 221（2015 年）稳定，dbus-broker 守护进程建在它上面。把 D-Bus 搬进内核的 kdbus 计划在争议中放弃，让位给更通用的 BUS1（来源：[Wikipedia D-Bus](https://en.wikipedia.org/wiki/D-Bus)）。

没有它桌面能不能活？能。2003 年前的桌面没有它，今天的轻量窗口管理器也可以完全不碰。代价是桌面件之间各自发明点对点协议，或者干脆放弃联动。

自研小桌面怎么办：舍实现，取一个概念，假装一个接口。舍的是 libdbus 与 bus daemon，单进程内任务栏与通知区说话就是函数调用，不需要中介。取的概念是服务激活：「图标点击拉起程序」与服务激活是同构物，都是点名一个名字，管家把真进程变出来，再把最初的输入递给它。假装的是进程内命名表：桌面件按名字报名，消息路由先做成一张进程内的表，形状学 D-Bus，给将来的真通道留位。

## 2 systemd 用户实例与桌面自启链

systemd 除了 PID 1，还给每个用户跑一个用户实例。系统侧的 `user@uid.service` 在用户登录时由 PAM 会话里的 pam_systemd 触发，拉起 `systemd --user`，此后的用户级服务都归它管，单元放在 `~/.config/systemd/user`、`/etc/systemd/user`、`/usr/lib/systemd/user` 三层，enable-linger 还能让服务在无登录会话时存活（来源：[ArchWiki systemd/User](https://wiki.archlinux.org/title/systemd/User)）。它解决的问题很朴素：用户级后台件（钥匙环、蓝牙代理、输入法框架、D-Bus 会话总线本身）也需要统一的生命周期、依赖顺序与重启策略，这正是 systemd 的本行。

图形会话的接入点是一对 target。systemd.io 的桌面集成文档规定：DE 在会话建立时先起 `graphical-session-pre.target`，环境变量齐了再起 `graphical-session.target`，会话结束时停掉它，挂在下面的用户服务随之收敛。同一时刻每用户只支持一个图形会话。DE 还要用 `systemctl --user import-environment` 把 DISPLAY、WAYLAND_DISPLAY 一类变量灌进用户实例，否则总线激活出来的服务看不见显示（来源：[systemd.io](https://systemd.io/DESKTOP_ENVIRONMENTS/)、[ArchWiki](https://wiki.archlinux.org/title/systemd/User)）。

自启链的全貌，从显示管理器到用户桌面件：

| 环节 | 动作 |
|---|---|
| 显示管理器（sddm、gdm） | 认证，开 PAM 会话，pam_systemd 拉起 systemd --user |
| DE 会话管理器 | 起 graphical-session-pre.target，导入环境变量 |
| graphical-session.target | 图形会话成立的标志，桌面用户服务都挂在下面 |
| XDG autostart | 扫 `$XDG_CONFIG_DIRS/autostart` 下的 .desktop 逐个拉起。DE 自己扫，或交给 systemd 的 systemd-xdg-autostart-generator 转成用户单元，由 xdg-desktop-autostart.target 聚合，OnlyShowIn 与 NotShowIn 经 ExecCondition 处理 |

自启规格本身只有一页草案：autostart spec 0.5，2006-02-13，DRAFT 状态保持至今，内容就是把 .desktop 文件放进 basedir 定义的 autostart 目录（来源：[autostart 规范](https://specifications.freedesktop.org/autostart-spec/latest/)）。systemd 还替 DE 规划了应用进程的归宿：session.slice、app.slice、background.slice 三个 slice 分档资源与 OOM 优先级，应用单元按 `app-<launcher>-<ApplicationID>.service` 命名（来源：[systemd.io](https://systemd.io/DESKTOP_ENVIRONMENTS/)）。

没有它能不能活？能。xinitrc 与 startx 这条前 systemd 链今天仍可用，DE 自己扫 autostart 目录也不依赖 systemd。现代发行版的默认路径却是上面这条链，绕开它就要自己处理用户服务的生命周期。

自研小桌面怎么办：舍整个 systemd，取「清单驱动的自启」这个形状。桌面本体在 Linux 侧由用户手动或脚本拉起。将来在自管 init 的系统上，init 直接拉桌面，恰好是 graphical-session 的最小同构：一个先行进程、一份环境、一个要拉起的图形会话。自启清单可以是一张编译进来的应用注册表，概念上与 autostart 目录里的 .desktop 文件同位，第 4 节会回到这里。

## 3 音频：从 PulseAudio 到 PipeWire

裸 ALSA 只给了「一个进程打开设备读写」的内核接口。多个进程同时出声要软件混音，每个应用单独调音量、音频走网络、蓝牙耳机热插拔时换路，这些需求它一个都不管。补这一层的就是音频服务器。

PulseAudio 由 Lennart Poettering 创建于 2004-07-17，初名 Polypaudio，2006 年改名。它在应用与 ALSA 之间做常驻服务器：软件混音、按应用调音量、网络音频、流在设备间迁移、蓝牙支持。2007 年 Fedora 8 第一个默认启用，早期 Ubuntu 8.04 的粗糙集成给它挣了多年骂名，Poettering 本人自嘲它是「the software that currently breaks your audio」（引语转引自 [Wikipedia PulseAudio](https://en.wikipedia.org/wiki/PulseAudio)）。Nokia N900 一类移动设备也曾用它。慢慢成熟后，它当了十几年桌面音频的事实标准。

PipeWire 是接棒者。Wim Taymans（GStreamer 共同作者）2015 年起步，初名 Pinos，目标是沙箱应用的视频流，2017 年改名，2018 年 11 月转 MIT 许可。2021 年 4 月 Fedora 34 第一个把桌面音频默认切到它，Ubuntu 22.10 与 Debian 12 跟进，2023 年 11 月发布 1.0（来源：[Wikipedia PipeWire](https://en.wikipedia.org/wiki/PipeWire)）。关键的兼容策略是 pipewire-pulse 实现了 PulseAudio 的客户端协议，JACK 应用也能直接跑，存量应用零改动。官方定位一句话：低延迟、图式的音频视频处理引擎，把 PulseAudio 与 JACK 的用例收进一个守护进程，并把 Flatpak 沙箱支持列为首要目标（来源：[pipewire.org](https://pipewire.org/)）。第 8 节的 ScreenCast portal 底下就是它。

教学视角看这条演化线最有味道的地方：音频整条链里没有一个窗口。窗口系统换了三代（X11 到 Wayland），音频服务器换了两代（PulseAudio 到 PipeWire），两条线几乎互不认识，唯一的交点是 portal 里的屏幕与摄像头捕获。

没有它能不能活？能。控制台蜂鸣与完全无音频的桌面都成立。代价是任何多媒体 App 都免谈。

自研小桌面怎么办：起步可以全舍，连 ALSA 都不碰。清单里的小游戏若将来要声音，第一个 beep 就是「直连设备还是走服务器」的判例，到时候先写设计文档再动手。概念上记住一件事：音频属于地基，真做时不该挂进任何 GUI 层，这与 PipeWire 自己的架构一致。

## 4 XDG 规格家族：七份纸面约定

XDG 规格是 freedesktop.org 上的一组纸面约定，只规定「文件放哪、写成什么格式」，不带任何实现。KDE 与 GNOME 都遵守它们，这也是两家桌面能互换应用程序的直接原因。逐份过。

### 4.1 basedir 规格（0.8，2021-05-08）

解决 dotfiles 在 `$HOME` 里乱长的问题。规格定义六个环境变量，给数据、配置、缓存、状态各划一个归处，另有一个由登录系统提供的运行时目录：

| 变量 | 默认值 | 用途 |
|---|---|---|
| XDG_DATA_HOME | `$HOME/.local/share` | 用户数据 |
| XDG_CONFIG_HOME | `$HOME/.config` | 用户配置 |
| XDG_STATE_HOME | `$HOME/.local/state` | 状态数据（日志、历史） |
| XDG_CACHE_HOME | `$HOME/.cache` | 缓存 |
| XDG_DATA_DIRS | `/usr/local/share:/usr/share` | 数据搜索路径 |
| XDG_CONFIG_DIRS | `/etc/xdg` | 配置搜索路径 |

XDG_RUNTIME_DIR 放运行时套接字与管道，规格不给默认值（来源：[basedir 规范](https://specifications.freedesktop.org/basedir-spec/latest/)）。

### 4.2 desktop entry 规格（1.5，2020-04-27）

定义 `.desktop` 文件：ini 格式的应用描述，键覆盖 Name、Exec、Icon、Type、Categories、NoDisplay 等，装在 `$XDG_DATA_DIRS` 的 applications/ 下。它同时是三样东西的数据源：菜单项、启动器图标、autostart 条目。规范引言明说 KDE 与 GNOME 采用了同一格式，互操作因此变简单（来源：[desktop-entry 规范](https://specifications.freedesktop.org/desktop-entry-spec/latest/)）。

### 4.3 icon theme 规格（0.13，2013-07-02）

定义图标主题的目录布局与 index.theme 文件：主题名、Inherits 继承链、每目录的尺寸与用途。查找顺序是 `$HOME/.icons`、`$XDG_DATA_DIRS/icons`、`/usr/share/pixmaps`，hicolor 永远在链尾兜底。这份规范基于 KDE 的图标系统写成，最初的服务对象就是 desktop entry 的 Icon 键（来源：[icon-theme 规范](https://specifications.freedesktop.org/icon-theme-spec/latest/)）。

### 4.4 shared-mime-info 规格（0.21，2018-10-02）

统一 GNOME、KDE、ROX 三家的文件类型判断。每份 MIME 类型由 glob（后缀与全名）加 magic（字节特征）共同识别，发行版用 update-mime-database 把散装 XML 编译成 mimeinfo.cache 加速查询（来源：[shared-mime-info 规范](https://specifications.freedesktop.org/shared-mime-info-spec/latest/)）。

### 4.5 recent files 规格（0.2，2002-08-12）

七份里最老的一份，GNOME 的 James Willcox 写的。规定最近文件列表存 `~/.recently-used`，XML 格式，至多 500 条，lockf 加锁，FAM 或轮询通知变更（来源：[recent-files 规范](https://specifications.freedesktop.org/recent-files-spec/latest/)）。它是「规格没跟上实现」的活标本：GTK 3 的 GtkRecentManager 后来改用 `$XDG_DATA_HOME/recently-used.xbel` 的 XBEL 格式，与规格写法分道（来源：[GTK 文档](https://docs.gtk.org/gtk3/class.RecentManager.html)及社区讨论）。

### 4.6 trash 规格（1.0，2014-01-02）

KDE 的 David Faure 与 Red Hat 的 Alexander Larsson 等人起草，草案 2004 年就开始了。删除的定义：把文件移进 `$XDG_DATA_HOME/Trash/files`，同目录 info/ 下写一份 `.trashinfo` 记原路径与删除时刻。外置挂载点用 `$topdir/.Trash`（需 sticky bit 且对他人不可写）与 `.Trash-$uid` 兜底。恢复与清空都是纯文件操作，不需要特权（来源：[trash 规范](https://specifications.freedesktop.org/trash-spec/latest/)）。

### 4.7 autostart 规格（0.5，2006-02-13，草案）

内容见第 2 节：autostart 目录里的 .desktop 文件，Hidden、OnlyShowIn 等键决定生效条件（来源：[autostart 规范](https://specifications.freedesktop.org/autostart-spec/latest/)）。

没有它们能不能活？每一份都只是约定，不守约定的桌面照跑，只是装不了别家的应用、混不进别人的桌面。它们是桌面之间互操作的共同语言，原理零门槛，采用成本接近于零。

自研小桌面怎么办：basedir 直接取，Linux 侧放配置与缓存时按它走，这是与宿主系统打交道的基本礼貌，成本只是读几个环境变量。desktop entry 半取：字段语义（Name、Icon、可执行物、是否显示）正对「报名字、给图标、造窗口内容」三件事，注册表内部结构照它的字段语义设计，但完整键集的解析不必做。trash 取，等文件管理器开工时做成教学判例，纯文件操作的删除与恢复是很好的入门站。MIME 半取：文件管理器双击打开需要一张类型表（双击 .txt 开文本编辑器），自养一张小表就够，判断思路沿用 shared-mime-info 的 glob，magic 不做。icon theme 舍，单色桌面用自己的图标注册。recent files 假装，一份 json 记最近文件就够，顺手把「这份规格败给了实现」的教训写进文档。

## 5 文本栈：一个字从字体文件到屏幕

现代 Linux 上一个字上屏要过四道工序，三个库各管一道，最后一道归工具箱。HarfBuzz 现任维护者 Behdad Esfahbod 在 2024 年的综述里有个总括：FreeType 加 HarfBuzz 构成开源世界的 OpenType 实现，与 Windows 的 DirectWrite、苹果的 CoreText 并列（来源：[State of Text Rendering 2024](https://behdad.org/doc/text2024/)）。

第一道是选字。fontconfig（Keith Packard，2000 年 12 月首发）按 family、style、size、语言覆盖度做 pattern 匹配，从系统字体目录挑出字体文件。fc-list 列字体，fc-match 演算匹配，配置是 XML 规则链，别名（sans-serif 到具体家族）与替换都在这里发生，另有磁盘缓存加速（来源：[fontconfig.org](https://www.fontconfig.org/)、[Wikipedia Fontconfig](https://en.wikipedia.org/wiki/Fontconfig)）。

第二道是整形。HarfBuzz（Behdad Esfahbod 2005 年动工）吃进 Unicode 串与方向信息，输出 glyph id 序列加每个字形的 advance 与 offset。连字（fi 合字）、阿拉伯文的位置变形、天城文的组合，全在这一步由 OpenType 的 GSUB（替换）与 GPOS（定位）表驱动。它自己不光栅化，README 自称 text shaping 界的 ffmpeg，Chrome、Firefox、Android、PlayStation 都在用它（来源：[harfbuzz/harfbuzz](https://github.com/harfbuzz/harfbuzz)）。复杂文本在整形前还要先过双向算法分线（FriBidi 或 ICU），那是另一个库的活。

第三道是光栅化。FreeType（David Turner 1995 年起步，与 Robert Wilhelm、Werner Lemberg 长期维护）是字体访问与光栅化的 C 库：按 glyph id 取字形轮廓，解释执行 TrueType 与 CFF 两类 hinting 指令，渲染成 8bit alpha 位图。Apple 的专利曾让它多年默认关 hinting，2010 年 5 月专利到期才放开。Android、iOS、macOS 的字体渲染底下都是它（来源：[Wikipedia FreeType](https://en.wikipedia.org/wiki/FreeType)、[freetype.org](https://www.freetype.org/)）。

第四道是合成。工具箱按 HarfBuzz 给的位置把 FreeType 给的位图 blit 到目标表面，配 gamma 校正与子像素定位。Behdad 的综述点名一个普遍缺陷：多数工具箱没做线性 alpha 混合的 gamma 校正，Qt 5.9 起对 CFF 字形是例外（来源：[behdad.org](https://behdad.org/doc/text2024/)）。

这段管线是 KDE 与 GNOME 共用的。Qt 的 QTextLayout 在所有平台走 HarfBuzz，GNOME 侧 Pango 包 FriBidi 加 HarfBuzz，两家再各自落到 fontconfig 加 FreeType（来源：[behdad.org](https://behdad.org/doc/text2024/)）。

没有它能不能活？点阵时代能：内置位图字体，码点到字形一对一。CJK 与比例字体一来，点阵方案要么自己烤大字号字库，要么走进上面这条管线。

自研小桌面怎么办：短期舍三件套，零第三方依赖的项目把 fontconfig、FreeType、HarfBuzz 都挡在外面，第一版继续点阵。中期的教学路线值得把四道工序写全：若上 TTF，自研最小实现可以控制在「英文等宽加 CJK 等宽」的范围，解析 sfnt 容器、查 cmap 表、光栅化轮廓，三步就是三个漂亮的教学站。整形先假装不存在，等宽场景下 Unicode 码点到 glyph 一对一，连字与组合文字明确写进「不支持清单」，这份简化本身就是教学材料。四个名字（选字、整形、光栅化、合成）先进词汇表，将来做抗锯齿文本时，gamma 问题也有现成的参照系。

## 6 剪贴板与选择：PRIMARY、CLIPBOARD、拖放

X11 的剪贴板与「一块公共内存」的想象完全不同。selection 是所有权协议：源应用向 X server 声明拥有某个 selection，数据始终留在源端，谁粘贴谁向当前所有者请求传输，传输前双方按 target 协商好用哪种格式（UTF8_STRING、image/png 等）。懒传输，大对象不占中间人内存。

这里有两条并行的 selection。PRIMARY 是「当前选中」，鼠标一划内容即入，中键粘贴，freedesktop 的剪贴板 wiki 直接把它叫作资深用户的彩蛋（easter egg）。CLIPBOARD 是显式复制粘贴通道，Ctrl+C 与 Ctrl+V 那套。两者独立，可同时持有不同内容。源进程退出前可把 CLIPBOARD 托管给 CLIPBOARD_MANAGER 代持，否则一关应用剪贴板就空了（来源：[ClipboardsWiki](https://www.freedesktop.org/wiki/Specifications/ClipboardsWiki/)）。

拖放是第三兄弟，机制同源：拖动开始时源持有 selection 并把动作（复制、移动、链接）告诉对方，XDND 协议处理途中的窗口切换与松手归属。同一份 wiki 说明了两者的关系：拖动本身就被解释为对 selection 的临时接管（来源同上）。

Wayland 侧沿用语义、换了中介：数据仍留在源客户端，由合成器牵线（wl_data_device 与 data source），命令行工具靠 wlr-data-control 与 primary-selection 两个协议接入，wl-clipboard 是参考实现（来源：[wl-clipboard](https://github.com/bugaevc/wl-clipboard)）。

没有它能不能活？不能算完整的桌面。终端第一天就需要 copy/paste，这是 App 清单里最基础的一件。

自研小桌面怎么办：这一项是八件里唯一「立刻取」的。X11 下 Xlib 的 selection 接口是系统库，不破零第三方依赖。终端是第一个消费者，剪贴板语义定成两份：CLIPBOARD 走显式复制，PRIMARY 走选中即有，终端传统里中键粘贴便宜又好用。单进程内先养一个桌面剪贴板件，接口形状按所有权模型设计（报名、set 数据、按 target 请求），分进程后这个件升级成所有权的代理持有者。拖放推迟，等图标与子程序都齐了再回来。

## 7 输入法：IBus、Fcitx5 与 text-input 协议

键盘事件到文字插入之间，拉丁语系几乎是一步，CJK 要三段：preedit（预编辑串，已拼未上）、候选窗（选字）、commit（上屏）。这三段语义塞不进「keydown 给码点」的模型，于是有输入法框架这一层。它要解决三件事：应用把光标位置与上下文告诉输入法，输入法把 preedit 与候选窗画到正确的地方，双方就何时上屏达成一致。

架构上，框架都是一个守护进程加一套引擎进程，通过总线与应用说话，应用侧靠 GTK、Qt 各自的 IM module 挂进文本控件，X11 时代还有 XIM 协议兜底。两强并立。IBus 是 Red Hat 的 Peng Huang 2008 年 8 月发布的产物，ibus-daemon 加引擎进程全走 D-Bus，2009 年起 Fedora 默认，Ubuntu 9.10 起替换 SCIM（来源：[Wikipedia IBus](https://en.wikipedia.org/wiki/Intelligent_Input_Bus)）。Fcitx 出身 2002 年（Yuking 发起），Fcitx 5 是 C++ 重写加插件架构，5.0 于 2020 年 11 月发布并从 GPL 转 LGPL，KDE 的跨发行版合作把它带进各发行版的软件库（来源：[Wikipedia Fcitx](https://en.wikipedia.org/wiki/Fcitx)）。GNOME 深度绑定 IBus，KDE 与 Fcitx5 走得近，这是两家桌面在此题上的分野。

Wayland 把这条链折成两段协议。应用到合成器走 text-input：v3 进了 wayland-protocols，是当前主流，v1 有 Weston 与 KWin 5.27 支持，v2 从未进 wayland-protocols，基本只有 KWin 用。合成器到输入法走 input-method：KWin 与 Weston 用 v1，wlroots 系用 v2。GNOME 的 Mutter 是异类，IM 侧直接说 IBus 的 D-Bus 协议，于是 Fcitx5 在 GNOME 下也得开 IBus 前端。KDE 这边合成器还用「虚拟键盘」挂载点拉起输入法进程（来源：[Fcitx 5 on Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland)，细节极全）。

没有它能不能活？纯英文桌面能。但中文用户绕不开它，这条链缺席意味着桌面永远只能收英文，这是真需求，只是排在窗口与终端之后。

自研小桌面怎么办：短期舍实现，KeyDown 码点直接当文字，文档明写「假装置入英文」。中期取概念：终端控件的文字入口预留 preedit 与 commit 两段语义的接口形状，教学上可以做一个内置的迷你拼音过滤器当判例，让「输入法的本质是文本过滤器」变成看得见的东西。Wayland 后端开工时把 text-input-v3 记进依赖清单。

## 8 portal：沙箱时代的权限中介

Flatpak 把应用关进 bubblewrap 沙箱：文件系统看不见，摄像头与屏幕摸不着，D-Bus 也只放行白名单。可应用总要开文件、发通知、共享屏幕。portal 就是为了翻这道墙才有的东西：一个标准的中介服务，应用通过 D-Bus 向它提出请求，它转给桌面后端的原生对话框，用户点了确定，结果（一个文件路径、一个屏幕句柄）再交回应用（来源：[xdg-desktop-portal 官方文档](https://flatpak.github.io/xdg-desktop-portal/docs/)）。

实现分两层。xdg-desktop-portal 是与桌面无关的前端，应用只认它。各桌面出后端：xdg-desktop-portal-gnome、-kde、-gtk、-wlr（wlroots 系）、-cosmic 等，文件选择器长什么样由后端决定。异步请求统一走 Request 对象模式，持续会话（如屏幕共享）走 Session。接口清单已经很长：FileChooser、OpenURI、Settings、Notification、Print、Screenshot、ScreenCast、Camera、GlobalShortcuts、Trash、Secret 等，ScreenCast 底下接的就是第 3 节的 PipeWire。中介也不限于沙箱应用，GTK 的环境变量一开，普通应用也走 portal，它正在变成「向桌面请求服务的标准方式」（来源：[官方文档](https://flatpak.github.io/xdg-desktop-portal/docs/)、[ArchWiki XDG Desktop Portal](https://wiki.archlinux.org/title/XDG_Desktop_Portal)、[who-t 的机制解析](http://who-t.blogspot.com/2021/08/flatpak-portals-how-do-they-work.html)）。

没有它能不能活？不跑 Flatpak 就不需要。它是八件里最年轻、也最依赖前面几件的一件：没有 D-Bus 就没有它，没有沙箱动机它不会诞生。

自研小桌面怎么办：实现全舍，概念记下。终局形态里子程序把像素递进桌面，事件由桌面递出。同一通道上可以再开一个「子程序请求桌面代办」的口，比如开文件对话框、申请全屏、报名字、要图标。这个口就是 portal 的同构物。它教的是「桌面是子程序的服务代理」这层抽象，正好咬合「桌面是启动器加窗口宿主」的定位。

## 9 八件套速览

| 件 | 解决什么 | 没有它能不能活 | 取舍示范 | 时点 |
|---|---|---|---|---|
| D-Bus | 进程间点名通信与懒启动 | 能，代价是各自发明协议 | 舍实现，取服务激活概念，假装进程内命名表 | 分进程前 |
| systemd 用户实例 | 用户级服务生命周期 | 能，自管自 | 舍 | 不做 |
| PipeWire 音频 | 多进程音频视频路由 | 能，无多媒体 | 全舍 | 要声音时再议 |
| basedir | 文件放哪的约定 | 能，但混不进宿主系统 | 取 | Linux 侧放配置时 |
| desktop entry | 应用描述与菜单数据源 | 能，装不了别家应用 | 半取字段语义 | 应用注册表设计时 |
| icon theme | 图标查找与主题继承 | 能 | 舍 | 不做 |
| shared-mime-info | 文件类型判断 | 能，双击打开没依据 | 半取 glob 思路 | 文件管理器 |
| recent files | 最近文件共享列表 | 能 | 假装，一份 json | 顺手 |
| trash | 删除与恢复的落点约定 | 能，删除即丢 | 取 | 文件管理器 |
| 文本栈 | 字体到屏幕四道工序 | 点阵可撑一阵 | 短期舍，概念全取，整形假装 | TTF 上屏时 |
| 剪贴板与选择 | 跨进程数据递送 | 缺它不算完整桌面 | 取，X11 立刻做 | 终端 |
| 输入法 | preedit 与 commit 语义 | 纯英文可以 | 短期舍，接口形状预留 | 终端定型后 |
| portal | 沙箱的权限中介 | 无沙箱即无需求 | 舍实现，记概念 | 跨进程协议设计时 |

## 启示

1. 单进程红利要兑现成将来的议题清单。八件基建里六件是多进程协作的伴生物，单进程起步把这层整体推迟，跨进程协议设计时应当把本篇每节的「回来的形状」列成议题，避免协议定型后才发现没给「桌面代办请求」留口。概念参考 D-Bus 的服务激活与 Flatpak 的 portal 代理。

2. 名字先于传输。D-Bus 的总线名加接口名说明：桌面件之间的通信先立名字与语义，再谈传输介质。进程内消息路由从第一天起就用「名字加方法加信号」的形状，即便它暂时只是一张函数表。概念参考 D-Bus 的 bus name。

3. 一张纸能统一两个桌面。basedir 与 desktop entry 没有任何实现，却让 KDE 与 GNOME 互换应用。落 Linux 侧配置时遵守 basedir，应用注册表字段语义照 desktop entry 设计，各后端沿用同一套约定，这就是跨后端共享桌面件的地基。概念参考 freedesktop 的 desktop entry 与 basedir 规格。

4. 删除是文件管理器的第一个教学判例。trash 规范把删除变成「移动加记事」两步纯文件操作，恢复与清空不需要特权，实现可控、语义完整，适合当 App 口的第一块砖。概念参考 freedesktop 的 trash 规格。

5. 文本上屏是四道工序，头四个名字先进词汇表。选字、整形、光栅化、合成。点阵方案的地位写成「第一版的光栅化替代物」，TTF 路线图与「假装整形不存在」的简化边界都写明，将来做抗锯齿文本时，gamma 校正问题有现成参照。概念参考 fontconfig 的字体匹配与 FreeType 的光栅化。

6. 剪贴板是第一个跨进程语义，X11 后端立刻可得。PRIMARY 与 CLIPBOARD 双选择模型用 Xlib 就能做，终端开工即取，所有权模型直接进单进程接口设计。概念参考 X11 的 PRIMARY 与 CLIPBOARD 双选择。

7. 输入法是中文用户的硬需求，接口先留形状。preedit 与 commit 两段语义在终端控件接口里预留，迷你拼音过滤器是理想的教学 App，Wayland 后端开工时补 text-input-v3。概念参考 IBus 的 input context。

8. 音频与 GUI 的彻底分离值得写进教学叙事。PipeWire 整条链没有窗口，窗口系统换了三代它自管自演化，这是「GUI 离操作系统很远」之外的第二条论据：桌面内部各层之间也离得很远。概念参考 PipeWire 的图式处理引擎。

## 参考来源

- [D-Bus Specification](https://dbus.freedesktop.org/doc/dbus-specification.html) 第 1 节：规范版本 0.43 与修订史、消息模型、服务激活与 SystemdService 键、激活环境继承
- [Wikipedia: D-Bus](https://en.wikipedia.org/wiki/D-Bus) 第 1 节：2002 年起源、DCOP 与 Bonobo 的替换关系、sd-bus 与 dbus-broker、kdbus 让位 BUS1
- [systemd.io: Desktop Environment Integration](https://systemd.io/DESKTOP_ENVIRONMENTS/) 第 2 节：graphical-session-pre 与 graphical-session.target、单图形会话、systemd-xdg-autostart-generator、slice 划分与 app 单元命名
- [ArchWiki: systemd/User](https://wiki.archlinux.org/title/systemd/User) 第 2 节：user@.service 与 pam_systemd、单元目录、linger、import-environment
- [Autostart Specification](https://specifications.freedesktop.org/autostart-spec/latest/) 第 2 节与 4.7 小节：版本 0.5 草案、autostart 目录
- [Wikipedia: PulseAudio](https://en.wikipedia.org/wiki/PulseAudio) 第 3 节：2004 年起源与改名、Fedora 8 默认、Poettering 引语、功能清单
- [pipewire.org](https://pipewire.org/) 第 3 节：项目自述（图式引擎、PulseAudio 与 JACK 兼容、沙箱目标）
- [Wikipedia: PipeWire](https://en.wikipedia.org/wiki/PipeWire) 第 3 节：Pinos 起源与改名、MIT 转让、Fedora 34 默认、1.0 时间线
- [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir-spec/latest/) 4.1 小节：版本 0.8、六个变量与默认值
- [Desktop Entry Specification](https://specifications.freedesktop.org/desktop-entry-spec/latest/) 4.2 小节：版本 1.5（2020-04-27）、KDE 与 GNOME 共用格式
- [Icon Theme Specification](https://specifications.freedesktop.org/icon-theme-spec/latest/) 4.3 小节：版本 0.13、出自 KDE 图标系统、查找顺序与 hicolor 兜底
- [Shared MIME-info Database](https://specifications.freedesktop.org/shared-mime-info-spec/latest/) 4.4 小节：版本 0.21、GNOME 与 KDE 与 ROX 统一、glob 加 magic
- [Recent File Storage Specification](https://specifications.freedesktop.org/recent-files-spec/latest/) 4.5 小节：版本 0.2、存储格式、500 条上限、lockf 加锁
- [GTK3 文档 GtkRecentManager](https://docs.gtk.org/gtk3/class.RecentManager.html) 4.5 小节：GTK 实际使用 recently-used.xbel
- [Trash Specification](https://specifications.freedesktop.org/trash-spec/latest/) 4.6 小节：版本 1.0、作者名单、目录结构与 .trashinfo、挂载点兜底
- [ClipboardsWiki](https://www.freedesktop.org/wiki/Specifications/ClipboardsWiki/) 第 6 节：PRIMARY 与 CLIPBOARD 语义、easter egg 说法、CLIPBOARD_MANAGER、拖放与 selection 的关系
- [wl-clipboard](https://github.com/bugaevc/wl-clipboard) 第 6 节：Wayland 侧 data-control 与 primary-selection 协议
- [State of Text Rendering 2024 (Behdad Esfahbod)](https://behdad.org/doc/text2024/) 第 5 节：三件套分工与总括、Qt 与 GTK 的用法、gamma 校正缺陷
- [harfbuzz/harfbuzz README](https://github.com/harfbuzz/harfbuzz) 第 5 节：HarfBuzz 定位、用户名单、OpenType 与 AAT 支持
- [fontconfig.org](https://www.fontconfig.org/) 第 5 节：fontconfig 自述与维护者
- [Wikipedia: Fontconfig](https://en.wikipedia.org/wiki/Fontconfig) 第 5 节：Keith Packard、2000 年首发
- [Wikipedia: FreeType](https://en.wikipedia.org/wiki/FreeType) 第 5 节：1995 年起步、三位维护者、专利史、使用范围
- [freetype.org](https://www.freetype.org/) 第 5 节：项目现状与发布节奏
- [Wikipedia: Intelligent Input Bus](https://en.wikipedia.org/wiki/Intelligent_Input_Bus) 第 7 节：IBus 出处、D-Bus 架构、Fedora 与 Ubuntu 的时间线
- [Wikipedia: Fcitx](https://en.wikipedia.org/wiki/Fcitx) 第 7 节：2002 年起源、Fcitx 5 重写、LGPL 转让、KDE 跨发行版合作
- [Fcitx 5 on Wayland](https://fcitx-im.org/wiki/Using_Fcitx_5_on_Wayland) 第 7 节：text-input 各版本归属、input-method v1 与 v2、Mutter 说 IBus 协议、虚拟键盘挂载
- [xdg-desktop-portal 官方文档](https://flatpak.github.io/xdg-desktop-portal/docs/) 第 8 节：机制、Request 与 Session、接口清单、后端家族
- [ArchWiki: XDG Desktop Portal](https://wiki.archlinux.org/title/XDG_Desktop_Portal) 第 8 节：非沙箱应用亦可使用
- [who-t: Flatpak portals how do they work](http://who-t.blogspot.com/2021/08/flatpak-portals-how-do-they-work.html) 第 8 节：portal 机制的实现视角解析
