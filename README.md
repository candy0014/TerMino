# TerMino

C++17 编写的终端方块游戏，支持 Linux 和 Windows。

由 candy0014 开发和维护；项目原创代码和文档沿用 [MIT 许可证](LICENSE)。第三方部分遵循各自许可，见 [第三方来源](THIRD_PARTY_NOTICES.md)。

- 自由练习、消行/限时/挖掘等目标、快速游戏、地图编辑与预设。
- 单机挑战、与 Bot 对战、回合制 Bot 对战及构造图形挑战。
- UDP / WebSocket 公网房间，多人对战、房主移交与观战。
- 原生回放、最近 30 场自动回放、挑战成绩与回放关联，以及重复另存为。
- 多套旋转系统与 Bot；尚需性能或设备验证的挑战保留【测试】标签。

Android 横屏预览版已提供可安装 APK，支持逐键编辑位置、大小、形状和透明度，页面操作采用固定左栏。
运行 `python3 tools/build_android.py` 生成 `dist/android/TerMino-android-preview.apk`；
构建、操作与真实验收范围见 [Android 适配说明](docs/ANDROID_ADAPTATION.md)。

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
OpenGL 预览 `tetr_gl` 已接入完整共享设置、地图/预设编辑器、挑战、Bot、双人、房间/观战和统计流程。
使用 `./compile.sh --frontend opengl` / `--frontend both`，Windows 使用
`./compile.ps1 -Frontend opengl` / `-Frontend both`；不带参数默认同时构建终端版和图形版。
仅构建终端版使用 `./compile.sh --frontend terminal` 或 `./compile.ps1 -Frontend terminal`，无需图形依赖。
日常编译脚本统一输出到 `./build`。Linux 双端构建后分别运行 `./build/tetr` 和
`./build/tetr_gl`；Windows 的 Visual Studio 构建位于 `build/Release` 等配置子目录，
MinGW 构建位于 `build`。切换 Windows 生成器时可用 `-BuildDirectory` 指定独立目录。
终端全部内容与工具持续保留；图形版全键盘操作，默认随包提供中文字体。第四阶段已补齐四组合 CI、本地独立打包、窗口配置及数据目录锁；原生设备与人工体验验收仍待完成。
依赖、预设、键盘操作、数据隔离和运行说明见 [图形预览说明](docs/OPENGL_PREVIEW.md)。

使用 CMake 3.21+ 和 Python 3.12+ 可分别制作默认初始数据的本地候选包：

```sh
python tools/package_frontends.py --preset terminal-linux
python tools/package_frontends.py --preset opengl-linux
# Windows 使用 terminal-windows / opengl-windows；MinGW 使用 *-windows-mingw
```

`both-linux` / `both-windows` 一次构建并输出两个独立包。预览发行见
[GitHub Releases](https://github.com/candy0014/TerMino/releases)，
包清单、完整性检查和实机待验项见 [双前端验收](docs/FRONTEND_ACCEPTANCE.md)。

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

终端版和图形版在允许撤回的模式中，均最多连续撤回最近 **256 个已锁定块**。
不足 256 块时可退至本局开头；更早的撤回快照自动释放。整局录像与回放时间轴跳转继续保留。

## 个人数据

所有个人数据保存在启动目录下的 `.tetr/`，不进入 Git：

| 位置 | 内容 |
| --- | --- |
| `config.json` | 两名玩家的设置、各平台键位、预设恢复状态、窗口参数及当前玩家 |
| `player1_linux.challenges` / `player2_linux.challenges`（Windows 对应 `*_windows.challenges`） | 个人挑战成绩及关联回放 |
| `maps.txt` / `presets.txt` | 个人地图与预设库 |
| `replay/` | 保存的录像与最近回放缓存 |

以上位置均相对于 `.tetr/`。旧版 `player*.ini`、`graphics.ini`、`active_player` 和根目录的 `keybinding*.txt` 可自动导入；正常保存统一写入 `config.json`。旧配置仍可通过显式导入使用。
两端支持 `--migrate-config`，可在不进入游戏的情况下完成迁移；迁移不会自动删除旧文件。
升级时自行保留整个 `.tetr/`；恢复默认可先备份，再移走个人配置目录和旧键位文件。
录像可使用绝对路径输入或另存；持久化引用尽量相对于项目目录，跨 Windows 盘符或共享根时保留绝对路径。

## 联机

默认公网入口为 `wss://termino.candy0014.icu/ws`。也可输入自建的完整 `ws://` / `wss://` 地址，
或用 `主机[:端口]` 连接 UDP 服务端。部署模板见 `deploy/`；令牌和本机部署参数不应提交到仓库。

更新客户端和服务端后，房间保留最近 30 场多人回放。参战者和观众均可在结束棋盘或统计页
按“保存回放”下载整场录像，也可从“房间对战回放”查看或另存；房间删除时清空服务端历史。
使用方式、保存边界和验证记录见 [房间多人回放](docs/ROOM_REPLAYS_20261021.md)。

## 打包与发布

```sh
bash package.sh --parallel 2
```

```powershell
.\package.ps1 -Parallel 2
```

两平台分别生成一个完整运行包：Linux 为 `.tar.gz`，Windows 为 `.zip`，输出到 `dist/`。
每包包含终端版 `tetr`、图形版 `tetr_gl`、FT7 工具 `tetr_ft7`、服务器 `tetr_server`、
字体、许可证、构建信息和 SHA256 校验文件。Linux 运行 `Play.sh`／`PlayGL.sh`／`Server.sh`，
Windows 运行 `Play.bat`／`PlayGL.bat`／`Server.bat`，分别启动终端、图形和服务器。

构建需要 CMake、Python 3.12+、Git 和 C++ 工具链。脚本构建 Release，静态捆绑图形依赖；
Linux 仍使用系统 libc、窗口库和 OpenGL 驱动。包内所有设置采用内置默认值，
`.tetr/replay` 为空，不带配置、键位、地图、预设、成绩、录像或服务器历史。
旧 `--data-dir`／`-DataDirectory` 参数仅为兼容保留，不再导入数据；本地 `.tetr` 不作修改。

`.github/workflows/release.yml` 构建、测试和检查两平台合包；推送
`v日期-preview.序号` 标签时另行构建 Android APK，通过检查后自动发布预览版。
预览发行说明保留原生设备待验项，Android CI APK 使用 debug 签名。
正式最新版仍需显式触发及原生体验验收通过。本地打包不会上传任何产物。

## 文档与源码

- [终端与 OpenGL 迁移及维护方案](docs/FRONTEND_MIGRATION.md)、[迁移前 Git 基线与回退](docs/FRONTEND_BASELINE.md)、[开发约定](CONTRIBUTING.md)
- [单机挑战](docs/SINGLE_PLAYER_CHALLENGES.md)、[挑战目录](docs/SINGLE_PLAYER_CHALLENGE_CATALOG.txt)
- [地图](docs/MAPS.md)、[预设](docs/PRESETS.md)、[多人房间](docs/MULTIPLAYER_ROOMS.md)
- [快速游戏规则](docs/QUICK_PLAY_RULES.md)、[模拟时间](docs/SIMULATION_TIME.md)
- [架构](docs/ARCHITECTURE.md)、[Bot 兼容性](docs/BOT_COMPATIBILITY.md)
- [zzztoj 来源与修补](src/bot/zzztoj/UPSTREAM.md)、[Cold Clear 来源](third_party/cold-clear/UPSTREAM.md)

`src/` 和 `include/` 为游戏实现，`tests/` 为回归测试，`tools/` 为分析及网关工具。
第三方源码保留原有许可证，发行包内附带相应通知。
