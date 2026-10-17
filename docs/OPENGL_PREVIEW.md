# OpenGL 第二阶段预览

图形版现在提供真实的 SDL3 / OpenGL 3.3 Core 窗口，可使用同一套游戏引擎游玩 Zen、40 行竞速、120 秒 Blitz，并打开单棋盘回放。保留原终端程序的全部功能和构建入口；图形版目前是迁移预览，完整功能迁移是第三阶段的验收条件。

## 构建

Linux：

```sh
./compile.sh --frontend terminal
./compile.sh --frontend opengl --test
./compile.sh --frontend both --test
```

输出分别在 `build/`、`build-opengl/`、`build-both/`。默认不带参数仍构建完整终端版；终端构建不查找或下载任何图形依赖。

Windows（安装 CMake 和 Visual Studio 2022 C++，或 MinGW）：

```powershell
./compile.ps1 -Frontend terminal
./compile.ps1 -Frontend opengl -Test
./compile.ps1 -Frontend both -Test
./compile.ps1 -Frontend opengl -Generator 'MinGW Makefiles'
```

也可以使用 `terminal-linux`、`opengl-linux`、`both-linux`、对应的 `*-windows` / `*-windows-mingw` CMake configure/build/test presets。例如：

```sh
cmake --preset opengl-linux
cmake --build --preset opengl-linux
ctest --preset opengl-linux
./build-opengl-linux/tetr_gl
```

图形目标名为 `tetr_gl`，终端目标仍为 `tetr`、`tetr_ft7`，独立服务端为 `tetr_server`。可以只构建图形可执行文件，或同时构建两端。纯图形配置不生成 `tetr` / `tetr_ft7`。

图形依赖优先使用系统 SDL3 ≥ 3.2 和 FreeType；缺少时下载带 SHA256 校验的 SDL 3.4.18、FreeType 2.14.1 源码，静态构建。可用 `-DTETR_GRAPHICS_FETCH_DEPS=OFF` 禁止下载。Linux 源码构建 SDL 需要窗口系统开发包（X11 或 Wayland / xkbcommon）和 OpenGL 驱动；例如 Debian/Ubuntu 的 `libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev libgl1-mesa-dev libfreetype6-dev`。系统 SDL 可直接使用发行版对应的开发包。

字体随构建复制到可执行文件旁的 `resources/fonts/`，运行不依赖源码绝对路径。预览 UI 使用英文，默认 DejaVu Sans；需要中文用户名、路径的完整字形时使用 `--font /path/to/CJK-font.ttf`。输入文本本身使用 UTF-8。原生输入法、中文覆盖和完整本地化仍需第三阶段实机验收。

## 仅用键盘操作

| 场景 | 操作 |
| --- | --- |
| 主菜单 | ↑/↓、Tab / Shift+Tab 移动可见焦点，Enter / 小键盘 Enter 选择，Esc 退出 |
| 打开回放 | 主菜单选择 Open Replay，输入路径；Backspace 删除，Ctrl+V 粘贴，Enter 打开，Esc 取消 |
| 游玩 | 沿用原生平台默认键位或显式导入的配置，窗口底部显示主绑定 |
| 默认移动与旋转 | ←/→ 移动，↓ 软降，Space 硬降，↑ 顺时针，Z 逆时针，A 180°，C Hold |
| 默认会话控制 | F2 暂停/继续，U 撤销，R 重试，P 返回菜单，Esc 返回菜单 |
| 回放 | Space / F2 暂停，←/→ 前后 10 秒，-/+ 倍速，R 重放，U 原引擎撤销，P / Esc 返回 |
| 完成对局 | S 保存原格式回放；R 重试，P / Esc 返回。保存路径显示在棋盘下方 |

保存使用现有录像编码和校验，写入 `.tetr/replay/gl-<唯一时间标识>.txt`，避免覆盖旧文件。已完成对局仍由原有逻辑保存最近回放。S 在正在进行的对局中不排队保存。单棋盘回放结束后仍可向后定位并继续播放。

SDL 输入按物理键映射到当前平台原有键码，因此现有多重绑定继续由共享 `BindingSet` 解释。默认覆盖字母、数字、方向、功能、修饰及常用小键盘键；特殊硬件/媒体键和跨平台重新绑定界面留待第三阶段。窗口失焦时抑制按住的键，恢复后须释放再按，避免卡键。图形版不启动 evdev、原生全局按键轮询或终端原始模式，不需要输入设备权限。

若导入配置将 Esc 绑定为游戏动作，沿用终端的绑定优先规则，Esc 执行该动作；使用配置中的菜单键返回。

## 数据与运行参数

预览默认使用内置设置；`--profile FILE` 只读导入已有平台配置与绑定，不自动保存、更换或迁移档案。选择预览模式时会暂时禁用 Bot 和挑战入口，不写回文件；完整配置、地图、预设、挑战及 Bot UI 后续逐项接入。请对照终端完成第三阶段的全部功能清单。

```sh
./build-opengl-linux/tetr_gl --data-dir /tmp/termino-gl-preview --play
./build-opengl-linux/tetr_gl --data-dir /tmp/termino-gl-preview --replay /absolute/demo.txt
./build-opengl-linux/tetr_gl --profile .tetr/player1_linux.ini
```

`--data-dir` 设置独立运行目录，录像和最近记录存入其 `.tetr/`；省略时使用当前工作目录。显式命令行文件路径在切换目录前解析。路径输入框沿用终端的回放路径规则：裸文件名在 `.tetr/replay`，`./` / `../` 相对运行目录，绝对路径直接使用。读取失败或校验错误显示错误并返回菜单，不静默开始新游戏。

`--no-vsync` 使用上限约 240 Hz 的显示循环；规则在独立工作线程运行。窗口大小和显示帧率不决定规则时间。棋盘根据尺寸缩放，保留顶部显示隐藏行、Hold / Next / Ghost 和基本统计；宽高比例保持稳定。风格为深色网格、青紫边框和明确焦点，没有震屏、粒子、拖尾或强闪烁。

## 验证与已知边界

`graphical_session` 在隔离目录验证共享游戏循环、Hold、移动旋转、硬降、撤销、暂停、保存、回放及原 App 入口的最终棋盘/分数/攻击一致性；测试不创建窗口。`sdl_input` 验证平台默认键位、纳秒时间戳、短按、重复过滤和失焦恢复。

有可用图形环境时，隐藏窗口检查（不在桌面显示）：

```sh
./build-opengl-linux/tetr_gl --data-dir /tmp/termino-gl-smoke \
  --smoke-test --screenshot /tmp/termino-gl-smoke.bmp
```

该命令经 SDL 事件队列注入菜单导航、移动、旋转、硬降和 Hold，要求共享引擎确实产生已放块和 Hold 后退出；失败返回非零。`--smoke-test` 和 `--frames N` 自动使用隐藏窗口，创建时即设置 `SDL_WINDOW_HIDDEN`，不会先闪现窗口；也可显式传入 `--hidden`。`--frames N --screenshot FILE.bmp` 可用于有界菜单/绘制检查。后续测试均使用隐藏窗口或 Xvfb 离屏环境，不在桌面显示测试界面。正常启动不隐藏窗口。CI 的 Linux 图形检查使用 Xvfb / Mesa；Windows 无窗口回归与编译不代替原生显卡、DPI、IME 和设备验收。

第二阶段仍临时链接原引擎需要的终端呈现适配库，并在图形会话期间关闭终端输出；没有复制规则或将 ANSI 画面当作图形界面。此桥接可在第三阶段逐步移除。图形版暂未接入完整设置菜单、地图编辑、预设、挑战界面、Bot 配置、房间/观战、双人及完整统计页；这些功能在终端中完整保留。图形预览不能替代正式终端版发布。

`cmake --install <build-dir> --prefix <empty-dir> --component OpenGLPreview` 安装预览程序、字体、说明和许可证，不复制本地 `.tetr`。该安装组件不是已完成的四组合正式发行流程；Linux 系统动态依赖和显卡驱动由运行环境提供，Windows MinGW 预览程序使用静态运行库。完整可携带打包、四组合发布仍在第四阶段验收。

## 图形依赖鸣谢

窗口与事件使用 SDL3，OpenGL 加载代码由 glad2 2.0.8 生成，默认字体为 DejaVu Sans。字体引擎使用 FreeType：Portions of this software are copyright © The FreeType Project (https://freetype.org). All rights reserved. 原始许可证随安装的 `resources/licenses/` 和 `resources/fonts/` 提供；固定源码版本及下载哈希见 `cmake/OpenGLFrontend.cmake`，glad 生成参数见 `third_party/glad/README.md`。
