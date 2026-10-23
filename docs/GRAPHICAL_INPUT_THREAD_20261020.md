# 图形输入与绘制分离（2026-10-20）

本次按用户要求，将 SDL 事件采集从图形绘制和显示限速中分离。此前的提前 ARR 计时修复见 [计时记录](DAS_TIMING_FIX_20261020.md)；本次继续保留原始时间戳、完整采样边界及两端共用的 DAS/ARR 实现。

## 线程与数据流

- **SDL 主线程**处理窗口事件、键盘、焦点、鼠标、文本/IME 和剪贴板；先提交整批物理按键，再执行界面回调。无事件时使用 1 ms 的事件等待，输入到来可提前唤醒；这不是操作系统的硬实时保证。
- **绘制线程**独占 OpenGL 绘制和 FreeType 资源，负责字体加载、动画、限帧与交换缓冲。主线程创建并解除上下文绑定，再经原生 EGL/GLX/WGL 接口交接；停止并回收绘制线程之后，主线程才销毁 SDL 上下文与窗口。
- **原会话线程**继续执行共享应用与玩法，不新增第二套移动或计时算法。每个玩家保留独立输入历史游标；忽略系统自动重复。

请求与反馈只在短锁内交换值或不可变指针。绘制、字体和原生交换不持有邮箱或会话锁。主线程使用轻量控制快照，不再每次采样复制完整棋盘与联网视图。鼠标命中区和输入法位置来自绘制反馈，命中前核对页面语义和窗口尺寸；失焦、物理键与鼠标模拟键的先后关系保持不变。文本事件持有独立内容，后续 SDL pump 不会使批次里的中文输入指针失效。

SDL3 的 [窗口与事件线程约束](https://wiki.libsdl.org/SDL3/FAQDevelopment)、[SDL_GL_MakeCurrent](https://wiki.libsdl.org/SDL3/SDL_GL_MakeCurrent) 和 [SDL_GL_SwapWindow](https://wiki.libsdl.org/SDL3/SDL_GL_SwapWindow) 要求这些 SDL 接口在主线程使用。因此工作线程使用主线程捕获的原生上下文和函数，不在工作线程调用 SDL 视频接口。EGL 显式恢复客户端 API，WGL 解绑保留 HDC 以兼容 SDL 已处理的驱动差异。

Wayland 使用原生 EGL、单独的 surface wrapper 和私有 frame-callback 队列，实际交换间隔设为零；启用垂直同步时，等待 compositor 回调最多 50 ms，与 SDL 的有界等待策略一致。使用 `wl_proxy_marshal_constructor_versioned`，保留 SDL 原有的 Wayland 1.18 基线，不增加 1.20 接口要求。绘制线程不能分派 SDL 的输入队列。正常隐藏、最小化或遮挡窗口跳过交换，避免不可见 surface 的回调阻塞退出。

## 回归入口

默认 CTest 中的 `frontend_handling` 复用同一个移动夹具，覆盖阈值、慢采样和原生/SDL 输入一致性，并在 30/60/240/1000 FPS 设置下阻塞首帧，同时提交按下、系统重复和松开；DAS=90 ms、ARR=4 ms 时，80 ms 短按只移动一格，120 ms 长按移动九格。另覆盖初始化失败清理及低帧率退出。`graphical_input` 合并 SDL 键位/时间戳/采样、焦点、文本所有权和鼠标模拟键取消检查；双人独立游标保留在既有共享输入回归中。

真实绘制线程测试先用 `cmake --build build --target graphical_render_thread_test` 显式构建，再运行；默认创建隐藏窗口，不进入普通 CTest 的无窗口环境：

```sh
SDL_VIDEODRIVER=offscreen ./build/tests/graphical_render_thread_test resources/fonts/NotoSansCJKsc-Regular.otf
SDL_VIDEODRIVER=x11 ./build/tests/graphical_render_thread_test resources/fonts/NotoSansCJKsc-Regular.otf
SDL_VIDEODRIVER=wayland ./build/tests/graphical_render_thread_test resources/fonts/NotoSansCJKsc-Regular.otf --hidden-swap
```

`graphical_render_thread_test` 使用真实共享会话与真实 GL 绘制线程：先由工作线程绘制并读回像素，再人为卡住首帧，短按移动后继续等待超过 DAS，确认没有额外重复，再确认 Hold 和硬降已生效，整个过程没有一帧完成。恢复绘制后核对反馈、窗口缩放和退出。Wayland `--hidden-swap` 仍为隐藏窗口，另外验证实际原生交换及有界回调等待。`--present` 仅供专用虚拟显示服务使用，并要求环境保护标志，不能在用户桌面运行。

同日按用户要求整理测试：删除重复的 `graphical_input_pump_test.cpp`、`graphical_gl_dispatch_test.cpp` 和 `sdl_input_test.cpp` 独立入口，将独有断言并入上述三个测试。移除临时 `--live` 探针及与阈值/采样矩阵重复的帧率扫描；关键故障回归继续默认执行。默认 CTest 注册由 170 项合并为 168 项。以下表格保留合并前输入修复的实际验收记录。

整理后的这批测试从 6 个源文件、759 行缩减为 3 个源文件、546 行。Linux 重新编译三个保留的测试，受影响的 7/7 项 CTest 通过（含共享输入、DAS 预充能和架构边界）；offscreen/EGL、X11/GLX 和 Wayland 隐藏绘制线程检查全部通过，含合并后的像素读回断言。本轮没有重跑完整 168 项或 Windows 测试，没有改动游戏实现；个人 `.tetr` 仍逐文件核对。

## 实际验证

测试使用独立临时目录，个人 `.tetr` 不参与运行。实际覆盖如下：

| 检查 | 结果 |
| --- | --- |
| Linux 双端构建和完整 CTest | `CMAKE_BUILD_PARALLEL_LEVEL=4 CTEST_PARALLEL_LEVEL=4 ./compile.sh --test --frontend both` 通过，**170/170**，含输入、完整图形应用、回放、联机、Bot 和架构边界；终端无 SDL/OpenGL/字体动态依赖 |
| 真实绘制线程停顿与游戏输入 | offscreen/EGL、X11/GLX、Wayland/EGL 隐藏窗口全部通过，含恢复绘制、缩放和退出 |
| 原生 GL 像素读回和交换 | Linux EGL/GLX 及 Wine WGL 隐藏检查通过；绘制停顿时主线程已收到按下和松开 |
| Wayland 垂直同步等待 | 隐藏原生交换及有界回调等待通过，无测试窗口显示 |
| 完整程序隐藏冒烟 | 与完整 CTest 的 Bot 压力检查并行，offscreen 30/1000 FPS、Wayland 30 FPS、X11 240 FPS 均通过共享引擎的 Drop/Hold 检查 |
| 截图与失败清理 | 三帧后输出 640×480 BMP 通过；校验后立即删除。字体加载失败正常返回错误并退出 |
| Windows | MinGW 对本次图形接入和三项新回归的语法检查通过；独立 GL 接入测试静态链接并在 Wine 的隐藏 WGL 窗口通过。未构建本次完整 Windows 客户端，未完成原生 Windows 设备、IME、DPI 或显卡验收 |
| 个人数据 | 验证前后 `.tetr` 的 214 个文件大小及 SHA256 完全一致 |

输入检查使用合成 SDL 事件；它验证采集、排队及共享引擎的行为，没有测量物理键盘或显示器的反馈延迟。操作系统或驱动本身的阻塞仍可能影响进程调度，但显示帧率和绘制等待不再直接控制输入采样。`docs/acceptance/native.json` 的未完成人工项继续保持待验。本次不创建或保留桌面截图。

修改限定于 OpenGL 的输入调度、绘制线程和反馈适配及对应回归；共享会话只新增轻量只读快照接口，终端目标不引入 SDL、OpenGL 或字体依赖。没有改变配置、回放、网络格式或发布状态。回退本次增量时保留此前计时修复和工作区其他修改，不替换或清空个人数据。
