# TerMino

C++17 编写的终端方块游戏，支持 Linux 和 Windows。

由 candy0014 开发和维护；项目原创代码和文档沿用 [MIT 许可证](LICENSE)。第三方部分遵循各自许可，见 [第三方来源](THIRD_PARTY_NOTICES.md)。

- 自由练习、消行/限时/挖掘等目标、快速游戏、地图编辑与预设。
- 单机挑战、与 Bot 对战、回合制 Bot 对战及构造图形挑战。
- UDP / WebSocket 公网房间，多人对战、房主移交与观战。
- 原生回放、最近 30 场自动回放、挑战成绩与回放关联，以及重复另存为。
- 多套旋转系统与 Bot；尚需性能或设备验证的挑战保留【测试】标签。

## 下载与运行

在仓库的 **Releases** 页面下载对应平台的完整压缩包，解压到可写目录：

- Linux x64：在终端运行 `./Play.sh`。
- Windows 10/11 x64：运行 `Play.bat`，建议使用 Windows Terminal 和支持中文的等宽字体。
- `Server.sh` / `Server.bat` 启动独立 UDP 服务端，默认端口 3000。

发行包及源码均不包含个人设置、个人地图/预设、挑战成绩或回放。
缺少配置文件时，程序使用代码中的完整默认值。不要只复制单个可执行文件。
Linux 的键盘输入需要可读取的输入设备；WSS 需要系统可信 CA 证书。

## 从源码构建

源码已包含带本地修补的 zzztoj 快照和 Cold Clear 参考源码，无需初始化 Git 子模块。
需要 CMake、C/C++17 工具链及线程支持。首次便携构建会下载校验哈希固定的网络依赖。

Linux：

```sh
bash compile.sh
./build/tetr
```

Windows（Visual Studio 2022 或更新版本的 C++ 桌面工具，或 MinGW-w64）：

```powershell
.\compile.ps1
```

也可直接使用 CMake：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --parallel 2
```

WebSocket 集成测试需要 Python 和锁定的依赖：

```sh
python -m pip install --require-hashes -r deploy/websocket-requirements.txt
python -m unittest discover -s tests -p '*test.py' -v
node tests/moch_pressure_test.cjs
```

启用 Python 依赖后重新配置 CMake，WebSocket 用例会加入 CTest。
需要外部官方引擎或自备录像的离线参照工具另见各脚本的用法；个人录像不随仓库提供。

前端迁移另提供独立 CMake 预设。Linux 使用 `terminal-linux`，Windows Visual Studio 使用
`terminal-windows`，Windows MinGW 使用 `terminal-windows-mingw`。例如：

```sh
cmake --preset terminal-linux
cmake --build --preset terminal-linux
ctest --preset terminal-linux
```

无窗口、无终端输入依赖的核心与视图检查使用 `frontend-headless-linux` / `frontend-headless-windows`。
单独构建服务端可配置 `-DTETR_BUILD_TERMINAL=OFF -DTETR_BUILD_SERVER=ON`。
OpenGL 第二阶段预览已提供 `tetr_gl`：SDL3/OpenGL 窗口、共享引擎游玩、单棋盘回放和全键盘导航。
使用 `./compile.sh --frontend opengl` / `--frontend both`，Windows 使用
`./compile.ps1 -Frontend opengl` / `-Frontend both`；不带参数仍构建完整终端版。
图形预览尚未覆盖终端全部界面，完整功能迁移在第三阶段验收；现有终端全部内容继续保留。
依赖、预设、键盘操作、数据隔离和运行说明见 [图形预览说明](docs/OPENGL_PREVIEW.md)。

## 默认操作

| 操作 | 按键 |
| --- | --- |
| 左移 / 右移 / 软降 | ← / → / ↓ |
| 硬降 | Space |
| 顺时针 / 逆时针 / 180°旋转 | ↑ / Z / A |
| Hold | C |
| 重试 / 设置 | R / P |
| 保存录像 / 撤回 / 暂停 | S / U / F2 |

两个玩家的默认物理按键相同，可在设置中分别修改。
Linux 与 Windows 使用各自的原生键码；首次运行不需要键位文件。

## 个人数据

所有个人数据保存在启动目录下的 `.tetr/`，不进入 Git：

| 位置 | 内容 |
| --- | --- |
| `player1_linux.ini` / `player2_linux.ini` | Linux 玩家配置 |
| `player1_windows.ini` / `player2_windows.ini` | Windows 玩家配置 |
| `*.ini.challenges` | 个人挑战成绩及关联回放 |
| `maps.txt` / `presets.txt` | 个人地图与预设库 |
| `replay/` | 保存的录像与最近回放缓存 |

以上位置均相对于 `.tetr/`。根目录的旧 `keybinding*.txt` 也属于个人配置，并被 Git 忽略。
升级时自行保留整个 `.tetr/`；恢复默认可先备份，再移走个人配置目录和旧键位文件。
录像可使用绝对路径输入或另存；持久化引用尽量相对于项目目录，跨 Windows 盘符或共享根时保留绝对路径。

## 联机

默认公网入口为 `wss://termino.candy0014.icu/ws`。也可输入自建的完整 `ws://` / `wss://` 地址，
或用 `主机[:端口]` 连接 UDP 服务端。部署模板见 `deploy/`；令牌和本机部署参数不应提交到仓库。

## 打包与发布

```sh
bash package.sh --parallel 2
```

```powershell
.\package.ps1 -Parallel 2
```

输出在 `dist/`，包含游戏、服务端、启动脚本、许可证及 SHA256 校验文件。
从干净源码打包使用完整默认设置，不带任何个人库或记录。

`.github/workflows/release.yml` 在推送 `v*` 标签后，在原生 Linux / Windows 环境构建、运行测试、
检查压缩包的默认配置和实际启动，然后将两平台归档及校验文件发布到 GitHub Releases。

## 文档与源码

- [终端与 OpenGL 迁移及维护方案](docs/FRONTEND_MIGRATION.md)、[迁移前 Git 基线与回退](docs/FRONTEND_BASELINE.md)、[开发约定](CONTRIBUTING.md)
- [单机挑战](docs/SINGLE_PLAYER_CHALLENGES.md)、[挑战目录](docs/SINGLE_PLAYER_CHALLENGE_CATALOG.txt)
- [地图](docs/MAPS.md)、[预设](docs/PRESETS.md)、[多人房间](docs/MULTIPLAYER_ROOMS.md)
- [快速游戏规则](docs/QUICK_PLAY_RULES.md)、[模拟时间](docs/SIMULATION_TIME.md)
- [架构](docs/ARCHITECTURE.md)、[Bot 兼容性](docs/BOT_COMPATIBILITY.md)
- [zzztoj 来源与修补](src/bot/zzztoj/UPSTREAM.md)、[Cold Clear 来源](third_party/cold-clear/UPSTREAM.md)

`src/` 和 `include/` 为游戏实现，`tests/` 为回归测试，`tools/` 为分析及网关工具。
第三方源码保留原有许可证，发行包内附带相应通知。
