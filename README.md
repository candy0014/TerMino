# TerMino · 终端方块

C++17 终端方块游戏，支持 Linux 和 Windows。玩法参考 TETR.IO Season 2 / Quick Play 2，支持自定义规则、地图与块序、回放、本地双人、公网房间及多个 BOT。

## 构建和运行

需要 CMake 3.18+ 和支持 C++17 的编译器。Windows 可使用 Visual Studio 2022 的 C++ 桌面开发组件或 MinGW-w64。WebSocket 使用 libcurl 8.14+；找不到合适版本时，CMake 自动下载并构建固定版本依赖。

```bash
git clone https://github.com/candy0014/TerMino.git
cd TerMino
./compile.sh
./build/tetr
```

Windows 在项目目录运行 `compile.bat`；也可以使用 CMake：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build --config Release --parallel 4
```

第三方 BOT 所需代码随源码提供，包含本项目使用的边界修复，无须初始化子模块。建议使用 Windows Terminal 或支持中文的等宽终端。Linux 可读取当前有权限访问的键盘设备；也支持终端输入。

首次启动使用默认配置。玩家配置、地图和预设自动保存在 `.tetr/`；回放保存在 `replay/`。这些运行数据不加入 Git。平台默认键位文件随源码提供。

## 操作和设置

开始界面默认 `R` 开始、`P` 设置、`Esc` 退出，提示会跟随实际绑定变化。在设置中使用方向键、Enter 和 Backspace 浏览，支持分页、数值输入和多键绑定。界面默认中文，可在外观中切换 English。

支持多个旋转系统、随机器、HOLD/NEXT、重力与锁定规则、攻击及垃圾参数、Quick Play 修改器、暂停和撤回。设置、预设和地图说明见：

- [地图与自定义块序](docs/MAPS.md)
- [预设](docs/PRESETS.md)
- [Quick Play 规则](docs/QUICK_PLAY_RULES.md)
- [模拟时间和轮流出块](docs/SIMULATION_TIME.md)
- [BOT 支持范围](docs/BOT_COMPATIBILITY.md)
- [Quick Play BOT 适配](docs/BOT_QUICK_PLAY_MODS.md)
- [Cold Clear C++ 移植](docs/BOT_COLDCLEAR.md)

本地双人使用一个窗口、独立的玩家配置和键位。回放支持暂停、变速、跳转和双方视角；录制包含实际规则及地图内容。

## 联机和房间

运行 `./build/tetr_server` 启动 UDP 中继，默认端口为 3000。客户端在「联机」中设置服务器地址和用户名；用户名默认使用 `GUEST-` 随机字符串，连接后修改会同步到服务器。

连接后先创建或加入房间。房间可公开或私有，空房名自动生成四个随机大写字母。大厅显示公开房间及人数，也支持按名称加入私有房间。当前每房最多两人，支持多个独立房间。

房间显示成员及房主。只有房主显示「切换房主」「强制使用房主配置」和「强制使用房主手感」；其他成员看到只读状态。房主退出时自动转交权限，最后一人离开后销毁房间。

强制房主配置默认开启，开始前同步本场规则、地图及完整块序；外观、键位和网络身份保持各自设置。强制手感默认关闭，开启时同步 DAS、ARR、DCD、软降和预输入参数。强制配置仅本场生效，不覆盖个人配置或地图库。所有成员校验并应用完整配置后，才初始化棋盘和倒计时。

对方画面使用实际动作结果和周期性压缩快照混合同步，带帧号、序号、版面校验及丢包恢复。联机设置中的重置保留联机模式；根重置保留网络选项、连接和房间。

公网可部署 WebSocket 网关，让客户端填写 `wss://你的域名/ws`。网关把每条连接转发到本地 UDP 中继，支持 TLS 反向代理和 Cloudflare Tunnel：

```bash
python3 -m pip install -r deploy/websocket-requirements.txt
python3 tools/websocket_gateway.py --host 127.0.0.1 --port 8080
```

中继需同时运行。`deploy/` 提供 systemd 服务模板，`tools/check_relay.py` 可检查 UDP 往返。启用任一房主强制开关时，所有玩家均需新版客户端。

## 打包与验证

Linux 使用 `./package.sh`，Windows 使用 `package.bat`。脚本生成便携包及校验文件，可通过 `--data-dir` 或 `-DataDirectory` 选择需要复制的个人运行数据；源码仓库不包含这些数据。

```bash
cmake -S . -B build-test -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-test --config Release --parallel 4
ctest --test-dir build-test -C Release --output-on-failure
```

WebSocket 集成测试需要 `deploy/websocket-requirements.txt` 中的 Python 依赖。`zzztoj_standalone/` 包含独立 BOT 验证程序；项目分层见 [架构说明](docs/ARCHITECTURE.md)。第三方来源与许可证见 [第三方说明](THIRD_PARTY_NOTICES.md)。
