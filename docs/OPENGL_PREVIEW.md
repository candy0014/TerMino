# OpenGL 双前端预览

`tetr_gl` 已接入原应用的完整设置、地图与预设编辑器、挑战目录、Bot、本地双人、网络房间、观战和统计流程。棋盘使用 OpenGL 原生绘制，菜单和编辑器消费共享应用画布；规则、参数校验、保存和协议继续使用原实现。终端程序及全部工具保持独立构建。当前仍为迁移预览；四组合本地打包和自动检查已接入，原生平台全功能与全键盘实机验收待完成，本轮不发行。

## 分别构建

Linux：

```sh
./compile.sh --frontend terminal
./compile.sh --frontend opengl --test
./compile.sh --frontend both --test
```

Windows（CMake、Visual Studio 2022 C++ 或 MinGW）：

```powershell
./compile.ps1 -Frontend terminal
./compile.ps1 -Frontend opengl -Test
./compile.ps1 -Frontend both -Test
./compile.ps1 -Frontend opengl -Generator 'MinGW Makefiles'
```

脚本目录为 `build/`、`build-opengl/`、`build-both/`；不带参数继续构建完整终端版。也可使用 CMake 的 `terminal-linux`、`opengl-linux`、`both-linux`，Windows 对应 `*-windows` / `*-windows-mingw` 预设：

```sh
cmake --preset opengl-linux
cmake --build --preset opengl-linux
ctest --preset opengl-linux
./build-opengl-linux/tetr_gl
```

图形目标是 `tetr_gl`；终端目标仍是 `tetr`、`tetr_ft7`，服务端是 `tetr_server`。纯图形配置不生成终端程序；纯终端配置不查找 SDL、OpenGL 或字体依赖。无窗口与无终端输入的契约检查使用 `frontend-headless-linux` / `frontend-headless-windows`。

图形构建优先使用系统 SDL3 ≥ 3.2、FreeType；缺少时下载有 SHA256 校验的 SDL 3.4.18、FreeType 2.14.1 并静态编译。`-DTETR_GRAPHICS_FETCH_DEPS=OFF` 禁止下载。Linux 构建 SDL 需要相应 X11 或 Wayland/xkbcommon 开发包，运行需要 OpenGL 3.3 Core 驱动。依赖清单见 `cmake/OpenGLFrontend.cmake`。中文字体 Noto Sans CJK SC 随程序安装，来源、版本和许可证见 [字体说明](../resources/fonts/README.md)。

## 完整键盘路径

| 流程 | 操作 |
| --- | --- |
| 启动 | 方向键、Tab / Shift+Tab 移动焦点，Enter 开始/设置/退出；原 R 开始、P 设置、Esc 退出仍可用 |
| 完整设置 | P 打开；↑/↓、PageUp/PageDown 选择，Enter/→ 进入或编辑，←/Backspace 返回，Esc 返回启动页，R 开始/重试；沿用原确认两次和恢复默认规则 |
| 设置内容 | 原全部设置与条件显示、13 类控制绑定、全部模式和目标、旋转/随机系统、模拟、回合制、全部 Bot、外观与语言 |
| 文本与路径 | SDL 文本输入与输入法组合串；←/→、Home/End、Backspace/Delete、Ctrl+A、Ctrl+V、Enter、Esc；中文姓名和路径使用 UTF-8 |
| 改键 | CONTROLS 中选动作，按下并释放要增加/移除的键；保留多重绑定及 Esc 优先规则 |
| 地图 | Maps → 新建/选中地图 → 编辑；保留原数字绘制、方向、Tab 切换版面/序列、搜索、撤销/重做、P 选项、S 保存及退出确认 |
| 预设 | Presets → 新建/编辑；保留定义/未定义参数、命名、保存、应用、层叠、撤销和删除 |
| 挑战 | Challenges → 原目录、挑战说明、开始/调整、成绩、资格和录像入口；使用同一 136 项固定挑战注册目录 |
| 网络 | NETWORK 中输入 UDP/WSS 地址并连接，创建/加入/查看房间，选择玩家身份；房主管理、强制规则、容量、准备和开始沿用原流程 |
| 对局 | 默认 ←/→ 移动，↓ 软降，Space 硬降，↑ 顺时针，Z 逆时针，A 180°，C Hold，U 撤销，F2 暂停，R 重试，P 设置；全部以当前玩家绑定为准 |
| 双人和观战 | 保留双方独立绑定、共享物理事件历史；原焦点切换、房间等待、观战及统计页操作继续生效 |
| 回放和结果 | MODEL 选择 Replay 并输入路径或选最近回放；原暂停、速度、定位、重放键有效；完成后 Tab 统计、方向键/翻页，S 输入文件名保存 |

鼠标不是任何操作的前提。本阶段未新增鼠标按钮。除启动页的附加焦点导航外，复杂界面直接使用原控制器，后续新增设置、挑战和编辑器操作能同时进入两端。

SDL 物理键转换到当前平台原键码，不读取 evdev 或全局键盘，也不创建终端原始输入会话。兼容字母、数字、导航、功能、左右修饰、小键盘、国际及媒体键；Linux 扩展映射来自固定 SDL 源码，Windows 兼容旧档案的通用 Shift/Ctrl/Alt。特殊硬件事件仍受操作系统和 SDL 能否投递限制，第四阶段需实机验证。失焦抑制按住状态，恢复后释放再按；文本事件与游戏动作分离。

## 档案与参数

默认读取并维护当前运行目录下原平台的玩家档案、键位、地图、预设、挑战历史和回放，两端使用相同格式。`--data-dir DIR` 切换到独立运行目录，其 `.tetr` 保存该次运行的数据；测试必须指定临时目录。命令行文件参数在切换目录前解析。

```sh
./build-opengl-linux/tetr_gl --data-dir /tmp/termino-gl --play
./build-opengl-linux/tetr_gl --data-dir /tmp/termino-gl --replay /absolute/demo.txt
./build-opengl-linux/tetr_gl --data-dir /tmp/termino-gl --profile /absolute/player1_linux.ini
```

`--profile FILE` / `--profile2 FILE` 导入双方档案，导入的 INI 不自动写回；编辑地图、预设、历史和录像仍使用运行目录。可以传入原终端的 0–2 个位置键位文件参数、`--play`、`--qp-continuous-cancel`；仍支持 `TETR_USER_CONFIG` 和 `TETR_USER_CONFIG_PLAYER2`。`tetr_ft7` 等离线工具保留独立可执行入口。完整参数通过 `--help` 查看。

回放保存使用原对话框、文件编码、校验和历史登记。裸文件名位于 `.tetr/replay`，`./` / `../` 相对运行目录，绝对路径按原规则处理，文件扩展名由输入决定。图形版不再使用第二阶段固定 `gl-*` 命名的简化保存入口。

棋盘保持稳定，支持原尺寸、隐藏行、Hold/Next/Ghost、出生提示、方块样式、垃圾、目标、Quick Play 状态、消除信息和多棋盘场景。深蓝黑、青紫边框和清晰焦点承担主要视觉效果，没有震屏、粒子、拖尾或强闪烁。规则在独立会话线程运行；`--no-vsync`、窗口大小及绘制频率不推进规则。

## 验证与边界

`frontend_ui` 验证值画布、嵌套/线程隔离、UTF-8 光标编辑和确认取消；`graphical_application` 在临时目录通过实际键盘驱动共享设置、地图、预设、改键、挑战、Bot、结果、保存、双人及混合客户端房间流程。`graphical_session` 保留录像与原 App 入口的语义一致性检查，`sdl_input` 验证键位、时间戳、重复过滤和焦点恢复。这些测试都不创建窗口。

实际 OpenGL 检查必须隐藏或离屏运行：

```sh
SDL_VIDEO_DRIVER=offscreen ./build-opengl-linux/tetr_gl \
  --data-dir /tmp/termino-gl-smoke --smoke-test --screenshot /tmp/termino-gl-smoke.bmp
```

`--smoke-test` 和 `--frames N` 自动从创建时隐藏窗口，`--smoke-test` 强制指定独立数据目录；也可显式传入 `--hidden`。正常启动显示窗口。禁止让自动化测试窗口出现在用户桌面。

仍保留过渡性终端呈现适配库以运行原共享应用；图形会话关闭 ANSI 输出，以值画布承载复杂界面，没有解析终端转义序列或复制游戏规则。应用控制器仍有阻塞循环；后续可逐项改为可推进调度器，不能以重构为由缩减内容。原生 Windows 输入法、DPI、真实显卡和 Linux 桌面设备体验的完整验收属于第四阶段，Wine 不代表原生 Windows 验收通过。

`cmake --install <build-dir> --prefix <empty-dir> --component OpenGLPreview` 安装图形程序、字体、说明和许可证，不复制个人 `.tetr`。四组合独立候选包改用 `tools/package_frontends.py` 和 `ClientTerminal` / `ClientOpenGL` 组件；说明见 [验收与打包](FRONTEND_ACCEPTANCE.md)。远程 CI 本轮未触发，正式发行等待实机验收与明确发行指令。

## 鸣谢

SDL3 提供窗口与事件，glad2 2.0.8 提供 OpenGL 加载代码，FreeType 提供字体引擎，Noto Sans CJK SC 提供默认中文字形。Portions of this software are copyright © The FreeType Project (https://freetype.org). All rights reserved. 原许可证随 `resources/licenses/`、`resources/fonts/` 安装；glad 生成参数见 `third_party/glad/README.md`。

## 窗口配置与运行目录

`.tetr/graphics.ini` 保存窗口尺寸、最大化、垂直同步和显示帧率上限。可用
`--window-size 1280x800 --frame-limit 240 --no-vsync` 覆盖，默认显示上限为 240 Hz，
不改变规则时钟。输入法组合期间，方向、删除、确认和取消按键交由输入法处理。

两端客户端共用 `.tetr/client.lock` 的操作系统排他锁；同目录已有客户端时拒绝启动，
进程结束（包含异常退出）后自动释放。需要多开时让各端使用独立运行目录；
图形端可用 `--data-dir DIR`。无需删除锁文件，旧配置与回放保留。
