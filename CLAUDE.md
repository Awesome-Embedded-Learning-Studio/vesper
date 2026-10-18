# vesper 开发工作流

桌面层独立仓，KDE 式起头：纯用户态工程，消费 visor（填它的宿主表），产出可拖窗口的桌面加终端。规划文档 `documents/`（桌面环境调研九篇与 visor 补件提案），路线与接缝见 `pack-26-desktop-handover.md`，抄写留档在 `PIN.md`。

## 角色分工（沿 visor 口径，2026-10-17 起）

- **代码：AI 直接实现加验证，作者 CR。** AI 在主树写码，跑绿必跑验收（`scripts/checks/acceptance.sh`），交付时说明改了什么、验了什么、边界在哪。作者过目后提交，或授权 AI 按批提交。仓库暂无远端，将来有也不擅自 push。
- **提交口径**：Conventional Commits，`type(scope): 描述`，正文写完即止，不加任何署名尾注（2026-10-18 作者明令：不准 Co-Authored-By）。提交时机由作者点头，不擅自 commit。
- **从零定位不动摇**：不 fork 现成工程当起点，零第三方依赖（Xlib、libwayland 算系统库），构建不用 presets，一律普通 `cmake -S -B`。
- **代码注释一律英文**（C++、CMake 同）。解释 why 的注释写成完整的句子，不复述代码本身。

## 必跑验收

最终验收必须运行 `scripts/checks/acceptance.sh`，覆盖四道：rel 构建与全部测试、ASan/UBSan 构建与全部测试、clang-format、clang-tidy。任何一步失败脚本即停，不得关检查、忽略退出码或跳过失败用例后报验收通过。TSan 道在 D4（事件循环扩成 poll 编排）前缺位：D0/D1 纯单线程离屏，届时补道数，覆盖范围与限制要跟结果一起写进批记。

## 借壳红线（借壳期适用，vendor 后作废）

- **不修改 visor 树内任何文件。** 缺件走上游提（提案束清单见 documents/visor-upgrade-design.md），不私养借壳源。demo_font 这类非库目标件例外，抄写进 `src/fonts/` 并在 PIN.md 留档。
- VISOR_ROOT 是 CMake 缓存变量，缺省 `/home/charliechen/visor`，配置时 `-DVISOR_ROOT=...` 改指他处。它只该指向一棵能过 visor 自家验收的 visor 树，vesper 的绿色不兜底一棵坏的 visor。
- tested-against 记在 PIN.md，visor 那边有新提交后重验并更新。
- **X11 只进肉眼通道。** visor.host.x11 只被 examples 的调试目标链接，不进主库、不进验收，验收以 fake host 为准。
- 待补：D4 补 TSan 道，D5 fbdev/evdev 抄写按 PIN 格式留档，D2 拍子表容量议题。

## 架构边界

- 输入事实与树语义归 visor（六种事件、捕获、焦点、脏区），桌面语义与宿主接线归 vesper（窗口、拖动、关闭、桌面摆放、将来的 spawn）。
- 桌面本体单进程，但「窗口内容递像素、桌面向窗口递事件」这条边从 D0 起以进程内接口的形状存在（fake session host 是它的第一块），D6 换真进程时桌面结构不推倒重来。
- 渲染回放纪律照 visor：on_draw 无副作用，绘制可重放。
- base 复用照 visor：新码先查 `visor::base` 的词汇（几何、位库、RingQueue），裸位运算不进新码。
