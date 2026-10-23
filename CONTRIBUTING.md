# 双前端开发和维护约定

TerMino 保留终端版的全部内容，并在同一游戏核心上新增 OpenGL 前端。需求、阶段、视觉风格及键盘要求见 [迁移方案](docs/FRONTEND_MIGRATION.md)，迁移前版本和恢复方法见 [Git 基线](docs/FRONTEND_BASELINE.md)。

Android 横屏适配的要求、源码阻碍、输入与配置契约及分阶段验收见
[Android 准备方案](docs/ANDROID_ADAPTATION.md)。Android 的“键位设置”已替换为
游玩虚拟键编辑器；位置、尺寸、形状和透明度逐键独立设置。其他页面命令采用统一的
固定左侧栏。桌面键盘配置和现有构建入口继续保留；Android 独立使用 `tools/build_android.py`
生成可安装预览 APK，模拟器和静态包检查不能代替真实设备验收。

## 每次更新的完成条件

1. 规则、挑战、Bot、回放、联网、存档及参数校验修改在共享层实现一次，两端消费同一语义结果。
2. 保留所有终端能力和入口；文件移动和重构不改变对外兼容行为。基线能力清单及现有回归测试持续运行。
3. 新增设置、挑战和操作同时进入两个前端的功能计划；图形版正式发布前逐项完成。后续正式更新缺少任一前端的等价操作时不能标记完成。
4. 图形界面的所有流程可仅使用键盘完成。鼠标提供附加入口；新控件必须有键盘焦点、操作、取消和返回路径。
5. 保持低延迟；显示帧率、垂直同步、字体加载及视觉动画不能改变玩法时间。界面采用酷炫且清晰的科技感样式；游玩特效默认保持清晰、数量有界，各项强度必须在图形版外观设置中可调并可关闭，终端版隐藏这些图形专属参数。
6. 保留现有配置、键位、地图、预设、挑战历史、回放和协议兼容。禁止以清空 `.tetr` 解决升级问题；打包使用默认初始配置。
7. 提交说明包含共享/两端影响、键盘路径、数据影响、验证结果和回退依据。迁移分为可独立验证及回退的提交。

## 边界检查

Runtime/Rules 保持纯语义边界。`tetr_presentation` 和 `tetr/frontend` 公共契约不得依赖 ANSI、终端渲染、SDL、OpenGL、原生设备输入、App 或 Network。回放/远端时间转换由 presentation context adapter 处理。前端不能直接修改收到的会话快照。

## 检查和发行

常规测试按模块合并为 `tetr_core_tests`、`tetr_rules_tests`、`tetr_engine_tests`、
`tetr_application_tests`、`tetr_network_tests`、`tetr_bot_tests`、
`tetr_bot_search_tests` 和 `tetr_graphical_tests`。测试程序统一输出到 `build/tests`
（Visual Studio 另有配置子目录），客户端仍位于 `build/tetr` 和 `build/tetr_gl`。
部分脚本入口、独立编译配置及原生检查保留单独程序；图形截图和绘制线程工具改为显式构建，
不会随普通测试构建自动生成。

`./compile.sh --test --frontend both` 继续构建并运行全部已启用的检查。
CTest 名称、超时、环境和资源锁保留，每项检查仍启动独立进程；
例如 `ctest --test-dir build -R '^gameplay_input_buffer$' --output-on-failure`。
直接运行套件时先用 `./build/tests/tetr_engine_tests --list` 查看模块入口，
再运行 `./build/tests/tetr_engine_tests gameplay_input_buffer_test`。
历史 `_test` CMake 目标现为套件内部对象模块，不再生成同名独立程序。
新增普通检查优先用 `tetr_test_module(目标 套件 源文件)` 加入现有套件，
入口为 `TetrTests::目标::run(int, char**)`，由公共驱动调用；只有需要独立环境的程序
使用 `tetr_test_program`。两种入口均统一保留 Release 下的断言。

日常运行受影响模块回归、前端契约测试和架构检查。完整图形会话检查为 `graphical_application`，画布/UTF-8 编辑契约为 `frontend_ui`；它们均不创建窗口。菜单、地图和预设通过共享控制器与 `UiFrame` 同步维护，棋盘新增信息同时扩展 `BoardView` 与两端绘制。共享变更运行终端完整 CTest；图形接入后加入两端全键盘、绘制与混合联网检查。Linux/Windows 的实际验证范围如实记录，Wine 结果不视为原生 Windows 显卡或输入设备验收。

图形测试使用隐藏窗口或离屏环境，不在用户桌面显示测试界面。`--smoke-test`、`--frames N` 已默认隐藏，其他自动化运行必须显式使用 `--hidden` 或隔离显示服务。

正式版本使用同一标签和提交构建 Linux/Windows 的终端与 OpenGL 包；全部验证通过后统一发布。图形版未覆盖终端全部内容前只能作为预览版分发。


候选包统一使用 `tools/package_frontends.py`（CMake 3.21+、Python 3.12+），由固定
`ClientBundle` 安装组件生成两平台合包，包含终端、图形、`tetr_ft7` 和服务端；
独立 `ClientTerminal` / `ClientOpenGL` 分包入口继续保留。
不复制本地 `.tetr` 或 `TETR_PORTABLE_DATA_DIR`。修改共享构建或打包清单后，运行
Python 分发回归与解压包检查；两平台合包（或四个独立前端包）应对应同一源码；本地未提交构建明确记录 dirty，
正式发行仍要求干净提交。不要复用另一提交的构建信息。

原生体验记录保存在 `docs/acceptance/native.json`，检查器为
`tools/check_frontend_acceptance.py`；输入法、DPI、实际设备或全键盘人工项未完成时保留
`pending`。CI 配置不能替代实际执行。发行 workflow 默认只生成候选包，显式发行仍需
通过原生记录门槛。第四阶段本轮只做本地工作，不推送、上传或向 GitHub 发行。
完整清单及命令见 [验收文档](docs/FRONTEND_ACCEPTANCE.md)。
