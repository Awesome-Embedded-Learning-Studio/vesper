# 视觉对照（2026-10-19 打磨批次）

这一组是字体与界面打磨前后的屏幕对照，采集自 X11 肉眼通道（Xvfb 上跑 `x11_desktop`，ffmpeg 抓 960x640，裁掉了鼠标指针）。画布本身是 480x320，宿主按 2 倍整数放大，所以截图里每个画布像素对应 2x2 个屏幕像素。

| 文件 | 内容 |
|---|---|
| [before.png](before.png) | 打磨前：5x7 全大写界面字、10 号灰度阈值切出的终端字、11 像素标题条 |
| [after-desk.png](after-desk.png) | 打磨后：两窗关闭，图标列与底部面板 |
| [after-about.png](after-about.png) | 打磨后：About 窗打开，比例界面字与加粗标题 |
| [after-terminal.png](after-terminal.png) | 打磨后：终端窗打开，12 号等宽终端字 |

复现采集：

```bash
cmake --build build --target x11_desktop
Xvfb :99 -screen 0 1024x768x24 &
DISPLAY=:99 ./build/examples/x11_desktop &
DISPLAY=:99 ffmpeg -f x11grab -video_size 960x640 -i :99 -frames:v 1 -draw_mouse 0 after.png
```

`after-terminal.png` 里的样文是临时往终端喂的两行字，方便看字模，正常运行时终端里是 bash 的输出。

字体链路、字号与底料记在 `PIN.md` 的字模条目里。
