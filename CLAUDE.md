# vesper 开发工作流

桌面层独立仓，KDE 式起头：纯用户态工程，消费 visor（填它的宿主表），产出可拖窗口的桌面加终端。规划文档 `documents/`（调研九篇、visor 补件提案、路线与 Cinux 接缝见 `documents/desktop-roadmap.md`）。抄来的文件把借据写在自己的头部注释（来源仓与 HEAD、改动摘要），不设集中的留档文档（2026-10-20 作者拍板）。

## 角色分工（沿 visor 口径，2026-10-17 起）

- **代码：AI 直接实现加验证，作者 CR。** AI 在主树写码，跑绿必跑验收（`scripts/checks/acceptance.sh`），交付时说明改了什么、验了什么、边界在哪。作者过目后提交，或授权 AI 按批提交。仓库暂无远端，将来有也不擅自 push。
- **提交口径**：Conventional Commits，`type(scope): 描述`，正文写完即止，不加任何署名尾注（2026-10-18 作者明令：不准 Co-Authored-By）。提交时机由作者点头，不擅自 commit。
- **从零定位不动摇**：不 fork 现成工程当起点，零第三方依赖（Xlib、libwayland 算系统库），构建不用 presets，一律普通 `cmake -S -B`。
- **代码注释一律英文**（C++、CMake 同）。解释 why 的注释写成完整的句子，不复述代码本身。

## 必跑验收

最终验收必须运行 `scripts/checks/acceptance.sh`，覆盖五道：rel 构建与全部测试、ASan/UBSan 构建与全部测试、TSan 构建与全部测试、clang-format、clang-tidy。任何一步失败脚本即停，不得关检查、忽略退出码或跳过失败用例后报验收通过。fbdev 实弹单列在 `scripts/vm/smoke.sh`（QEMU 加 KVM 的裸机彩排，串口标记加截屏像素判例），不进五道：没有 `/dev/fb0` 的开发机跑不了它，内核与 rootfs 的构建产物也只构建一次缓存着用。

## 借壳红线（借壳期适用，vendor 后作废）

- **不修改 visor 树内任何文件。** 缺件走上游提（提案束清单见 documents/visor/upgrade-design.md），不私养借壳源。非库目标件例外（Cinux-GUI 的 host 接线、脚本这类），抄写进对应目录，借据写在文件头。
- 借壳源唯一：`third_party/visor` 子模块（2026-10-20 作者拍板，不留 VISOR_ROOT 一类逃生口）。钉就是 vesper 历史里的 gitlink，升级等于子模块指针前移一笔提交，两边验收都绿了才动。并排开发在子模块里 checkout 分支干活。
- tested-against 记在本节：visor @ `2acb825`（wayland 后端、按键重复、事件族头分家、区域成批提交，主树消费方 include 随批对齐族头，含 `d8fc89f` 像素层三档落地，2026-10-22 实测全绿）。
- **X11 与 Wayland 只进肉眼通道。** visor.host.x11、visor.host.wayland 只被 examples 的调试目标链接，不进主库、不进验收，验收以 fake host 为准。
- 待补：终端 16 色 SGR 依赖 visor 侧逐笔墨色绘制原语（现绘制口只认七个语义槽位），已记为上游议题。D5b 按 roadmap 字面收官（wayland 后端与 headless 判例在上游，扩色交付在链路上）；D2 议题已随 visor `5bc71dc` 在上游消失，双击手势随 `49f95c0` 落在 visor 路由层，本仓不再持有这两项。

## 架构边界

- 输入事实与树语义归 visor（六种事件、捕获、焦点、脏区），桌面语义与宿主接线归 vesper（窗口、拖动、关闭、桌面摆放、将来的 spawn）。
- 桌面本体单进程，但「窗口内容递像素、桌面向窗口递事件」这条边从 D0 起以进程内接口的形状存在（fake session host 是它的第一块），D6 换真进程时桌面结构不推倒重来。
- 渲染回放纪律照 visor：on_draw 无副作用，绘制可重放。
- base 复用照 visor：新码先查 `visor::base` 的词汇（几何、位库、RingQueue），裸位运算不进新码。
