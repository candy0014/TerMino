# 双前端验收与本地候选包

第四阶段以 `cd5b9f47b554de9ba495c7238ae6f01fd5c9afda` 为起点。
本次范围是本地工程、自动化检查和四组合候选包；用户明确要求不向 GitHub 发行，
因此不推送提交/标签、不上传产物，也不创建或修改 GitHub Release。

## 构建与打包

需要 CMake 3.21+、Git、对应 C++ 工具链；打包和检查脚本使用 Python 3.12 或更新版本。
保持原 `compile.sh` / `compile.ps1` 和终端打包入口。新增统一入口：

```sh
python tools/package_frontends.py --preset both-linux --frontend both
python tools/package_frontends.py --preset terminal-linux --frontend terminal
python tools/package_frontends.py --preset opengl-linux --frontend opengl
```

Windows 使用 `both-windows` / `terminal-windows` / `opengl-windows`，
MinGW 使用对应的 `*-windows-mingw`。`both` 生成两个独立压缩包。
也可对已经配置的目录使用 `--build-dir DIR --frontend both`；
`--no-build` 只安装现有程序，不改变编译选项。跨环境安装后，
`--stage-dir DIR` 可将一个已安装组件归档。

统一打包从 `ClientTerminal` / `ClientOpenGL` 组件复制固定文件清单，
保留终端、FT7 与可选服务端，并为图形版提供字体、许可证和说明。
不读取 `TETR_PORTABLE_DATA_DIR`，不复制个人 `.tetr`、成绩或回放。
空 `.tetr/replay` 目录使用内置初始配置。已有本地数据目录不清理。

终端独立 portable 构建保持原完全静态方案；Linux 图形/双端 portable 构建
使用 `TETR_PORTABLE_FULL_STATIC=OFF`，固定 SDL/FreeType 源码静态编译，
系统窗口库、libc 和 OpenGL 驱动仍有平台要求。普通构建的包会记录相应构建信息，
不宣称可运行于任意 Linux 发行版。正式兼容基线使用 Ubuntu 24.04 CI 构建验证。

每包包含 `BUILD.json`、逐文件 SHA256 的 `PACKAGE.json` 和压缩包 SHA256。
同组四包必须来自同一源码提交；工作区未提交时只允许显式 `--allow-dirty`
制作本地测试候选包。脚本不会覆盖已有候选包。

## 全键盘与数据检查

| 范围 | 自动化证据 | 实机补充 |
| --- | --- | --- |
| 内容保留 | 139 个设置标识、136 项固定挑战、原动作/模式/CLI 目录检查 | 逐类挑战和所有 Bot 的实际体验 |
| 菜单和编辑器 | 原共享控制器回归；图形设置、改键、地图、预设、统计及录像 | 无鼠标完成完整人工清单 |
| 输入法和文本 | SDL 组合/提交、UTF-8 编辑、粘贴、确认、取消；组合期间不误改字段 | 中文输入法候选选择、组合取消及平台快捷键 |
| 焦点和双人 | 事件历史、左右修饰、多重绑定、失焦释放和独立玩家输入 | 真键盘、Alt+Tab、窗口切换 |
| 窗口和 DPI | 缩放绘制、窗口配置边界和原子保存；隐藏窗口多尺寸检查 | 原生 Windows 高 DPI、多显示器与缩放变化 |
| 联网和回放 | UDP/WSS、房间管理、观战、混合入口、交叉录像和结果回归 | 跨机器 Linux/Windows 混合房间 |
| 数据保护 | 两端运行目录排他锁、退出后重新获取；隔离目录和默认包审计 | 异常退出和升级备份的现场演练 |

所有图形自动化从创建时隐藏或在离屏显示服务运行，不在用户桌面显示窗口。
输入法事件注入不等于真实输入法验证，Wine 不等于原生 Windows 验收。
不可用的原生环境项目标为待验，不能以测试脚本通过替代。

`.tetr/graphics.ini` 独立保存窗口宽高、最大化、垂直同步和显示帧率上限；
不改变原玩家配置格式。`--window-size WIDTHxHEIGHT`、`--frame-limit FPS`、
`--vsync` / `--no-vsync` 提供键盘/命令行入口，规则时钟独立于显示帧率。
终端和图形客户端启动时获取同一目录的 `.tetr/client.lock`；占用时退出并提示，
操作系统在进程退出后释放锁，锁文件保留。多开使用不同运行目录。

## 原生验收状态与发行门槛

本地结果见本节后续记录及 Git 忽略的 `build-migration-backups/phase4-20261017/`。
当前机器为 Linux；MinGW/Wine 可检查 Windows 编译与部分执行，不能完成 Windows
真实设备、输入法、DPI、窗口管理和驱动验收。用户同时要求不显示测试窗口，
因此本轮不进行可见的人工游玩验收。

发布前应为 Linux/Windows 的终端和 OpenGL 分别记录源码提交、系统、编译器、
完整回归、键盘清单、DPI/IME/设备结果与包 SHA256。所有必需实机项目通过前，
产物保留为本地候选包。CI 配置存在不代表远程 CI 已执行。

本次不触发 GitHub workflow，也不使用 GitHub Release API。后续是否正式发行
由用户另行决定，不能通过推送版本标签自动绕过验收。

## 本轮自动化结果（2026-10-17）

| 检查 | 实际结果 |
| --- | --- |
| Linux 终端完整 CTest | 161/161 通过，终端独立配置不查找图形依赖 |
| Linux 图形 CTest | 8/8 通过，含完整共享应用、输入法事件和窗口配置 |
| Linux 无界面 CTest | 5/5 通过 |
| Windows MinGW / Wine | 四个可执行目标构建通过，8/8 前端回归与隐藏 WGL 冒烟通过 |
| Python / Node | 86 项 Python 回归通过；Node 压力检查通过 |
| 解压包运行 | Linux 原生、Windows Wine 下的隐藏图形、终端游玩/回放、FT7 与服务端通过 |
| 跨端数据与互斥 | 两平台终端录像进入图形版通过；同目录图形/终端互斥、强制结束后重新获取锁通过 |
| 原生 Windows / 人工体验 / 远程 CI | 未执行，继续待验 |

本地候选包放在 Git 忽略的 `dist/phase4-local/`，校验报告为该目录下
`validation.json`；同目录提供四包各自的 SHA256。包版本使用 `local-<源码提交前12位>`，
每包 `PACKAGE.json` 记录构建平台、编译器、来源、逐文件哈希和实际运行库依赖。
Linux 包在当前机器构建，不作为 Ubuntu 24.04 正式兼容基线的已验证产物。
Windows 便携包静态链接 SDL/FreeType 与 C++ 运行库，仍需操作系统 OpenGL 3.3 驱动。

```sh
# 原生 Linux 检查，Windows 包仅检查清单
SDL_VIDEO_DRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  python tools/check_frontend_distribution.py dist/phase4-local --matrix --cross-frontends
# Linux 已安装 Wine 时，可额外执行 Windows 包（仍不算原生 Windows）
SDL_VIDEO_DRIVER=offscreen LIBGL_ALWAYS_SOFTWARE=1 \
  python tools/check_frontend_distribution.py dist/phase4-local --matrix --wine --cross-frontends
```

后续更新先修改共享规则/控制器与两端显示，再执行相应回归；共享行为变化执行完整
终端 CTest 和图形契约/应用回归。重新配置并构建当前提交后制作四包，避免使用旧提交
构建信息。逐项完成实机表后，将环境、提交、检查项和证据填入
`docs/acceptance/native.json`，运行：

```sh
python tools/check_frontend_acceptance.py docs/acceptance/native.json --commit <源码提交>
```

当前模板保留 `pending`，该检查应拒绝正式提升。若之后只提交文档验收记录，可引用
相同实现的之前提交；实现、资源或构建改变会使旧验收记录失效。
发行 workflow 只有显式 `workflow_dispatch` 且 `publish=true` 才进入发布步骤，
还需四包同一干净提交和原生记录通过。版本标签推送仅构建候选包。
本轮不推送标签，不运行该 workflow，不发布 GitHub Release。
