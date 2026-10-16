# 单窗口抢七与 TETR.IO 回放：实施日志

## 用户要求摘要

- 额外入口运行原项目的单窗口本地对战，保留原规则、原 UI、原自动移动过程。
- Player 1：`bot_chatgpt`；Player 2：`zzztoj_botris`。
- 双方 `pps of autoplay = simulation pps = 4`，`turn piece = 1`。
- 抢七：任意一方达到 7 胜即停止；单局没有时间或块数上限。
- 输出完整多局 `.ttrm`，在 **TETR.IO 客户端** 中观看，不依赖本项目的回放播放器。
- **所有对局合并到同一个最终 `match.ttrm`**，其中每局同时包含双方完整操作，不让用户手动拼接。
- 仅在此入口临时切换包随机器、垃圾洞随机器；不改普通入口的默认随机流。
- PPS=4 已由主项目计算并执行移动方式，必须记录真实执行动作，不另造 BOT 路径。
- 特别区分 `S`（下移一格）、`s`（软降到底）、`H`（硬降并锁定）；需要保留后续 Spin。
- 不修改当前稳定 BOT 策略和估价。本项不是新的 BOT 优化实验。

## 当前状态（2026-09-25）

抢七入口、真实动作采集、临时随机 profile、多局 `.ttrm` 导出已接通。
正式无上限 BOT 抢七已完成：主种子 `20260925`，**bot_chatgpt 7:3 zzztoj_botris**。
十局全部合并到 [`replay/ft7_20260925_verified/match.ttrm`](../replay/ft7_20260925_verified/match.ttrm)，
共 12,699 次落子通过官方引擎逐块核验，双方的十局时间轴及快照跳转续播均通过。
观看只需这一个 `.ttrm`；同目录的 `.json` 用于审计，不是观看依赖。
本次请使用上述 verified 文件，**不要直接使用 `runlogs/ft7_dev1/match.ttrm` 的早期开发捕获**；
该原始文件保留用于审计，缺少随后补齐的原项目旋转规则事件。

## 运行

Linux：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target tetr_ft7 -j 4
./build/tetr_ft7
# 指定一个不存在的新目录和可复现的主种子
./build/tetr_ft7 --output replay/my_ft7 --seed 20260925
```

Windows（Visual Studio 多配置生成器）：

```powershell
cmake -S . -B build
cmake --build build --config Release --target tetr_ft7
.\build\Release\tetr_ft7.exe --output replay/my_ft7 --seed 20260925
```

MinGW/Ninja 单配置生成器的程序一般位于 `build/tetr_ft7.exe`。
Windows 路径采用 C++ filesystem，控制台输出临时切换 UTF-8 后恢复。
本轮没有 Windows 编译器，Windows 实机运行尚未验收。

- 不加参数时显示原项目双棋盘终端界面；不是另写的对战模拟器或 UI。
- `--headless` 仅关闭终端绘制，用于无人值守验收；不改变规则、BOT、PPS 或回合。
- 不加载/保存 `.tetr/*.ini`、不改键位文件；此入口禁用交互改设置，Ctrl+C 可中止进程。
- 忽略测试限块、限秒、限挖掘行数；拒绝非 1 的 `TETR_TEST_CLOCK_SCALE`，防止继承加速时钟。
- 默认输出 `replay/ft7_<时间戳>_<主种子>/`；明确拒绝覆盖已有目录/文件。
- 每局保存 `round_N.audit.json` 和累计的 `through_round_N.ttrm`；完成抢七后另存 `match.ttrm`。
  `through_round_N.ttrm` 本身包含第 1 到 N 局；最终 `match.ttrm` 的 `replay.rounds`
  一次性包含整场所有对局以及双方数据，另有总比分 `leaderboard`。备份不是需要拼接的分片。
- 新版本先写 `.part` 再原子改名；写入途中退出不会留下伪装成完整回放的 `.ttrm`。
- 中止时只保证已经结束的各局 checkpoint 完整；没有宣称未结束的一局是完整对局。
- 将 `match.ttrm` 拖入 TETR.IO 客户端的回放界面观看；不要用本项目的 `.txt` 播放器打开。

## 架构与规则边界

- `src/tetr_ft7.cpp`：组合两个原 `GameContext`，使用 `App::initialize_game/run`、
  原本地双向通道、胜负判定、渲染与低 PPS `movement_script`，固定 BOT 9/4、PPS 4/4、turn 1。
- `FirstToSeven` 只接收原主机产生的单一胜者结果；比分必须每局恰加一，到 7 不再开新局。
- `TetrioRecorder` 为每个玩家独立实例，记录 HOLD、实际命令及位移、真正锁定和收到的垃圾包。
  脚本 H 与 placement hook 不重复锁定；不会用目标落点替代移动路径。
- `TetrioRandom` 仅在附有此 recorder 的本地比赛启用：Lehmer 16807 / 2147483647，
  7-bag 原始顺序 `Z L O S I J T`，倒序 Fisher–Yates；包与洞口使用独立同种子流。
  垃圾整包被抵消时也推进洞口 RNG，部分抵消或 8 行截断后续包不提前换洞。
  卸除 recorder 后原 mt19937 随机流恢复，有初始化回归测试。
- 回放 `boardbuffer=100` 与主项目隐藏区一致，保留 Clutch Clear 空间；可见区仍为 10×20。
- 旋转规则使用主项目的真实 catalog，通过每局第 0 帧的 `custom/constants` IGE 携带。
  主项目 I 块 `3→0` 的一项踢墙与官方 SRS+ 不同（原项目右 2 下 1，官方右 1 下 2）；
  I 块的 180 度踢墙也不是官方默认表。
  因此不能仅写 `kickset=SRS+` 就假定完全一致。导出器从原 catalog 生成完整 JLSTZ/I 踢墙表，
  不改主项目规则、不替换按键、不修改棋盘；客户端原生支持该规则事件及跳转快照。
- 主机已经处理过网络 passthrough，所以回放记录的是处理后的实际入包，设置
  `passthrough=full` 避免客户端再次抵消。垃圾延迟使用模拟时钟 18 帧，即 0.3 秒。
- 原项目与官方 v19 的 Opener Phase 判定不同；不改原项目规则。回放使用
  `openerphase=0`，只在主机真的双倍抵消的落子前发客户端支持的
  `custom/setoptions/cancelmultiplier` 事件，其余恢复 1。没有直接修改回放棋盘。
- **时间口径**：现场 UI 保留原真实等待、搜索和移动节奏；`.ttrm` 使用模拟时钟，
  每方每块 15 帧（4 PPS），P1/P2 锁定相位分别为 7/14，轮流显示。
  回放不保留机器搜索耗时和原真实等待长度，因此不是现场等时长录像。
  主项目在 turn=1 时的实墙钟总吞吐不能简单当成单方 4 PPS。
- `.ttrm` 初始汇总以主项目数据为依据；其分数不含客户端所有软降/硬降、全块 Spin 计分项，
  主机没有 finesse 统计，原始导出中该项为 0。观看时客户端会自行推进真实游戏状态。
  如需客户端口径的完整附加统计，使用下方可选的官方引擎核验/补全工具；输入文件不会覆盖。

## 官方引擎核验与附加统计

```sh
node tools/verify_tetrio_match.cjs /path/to/tetrio.js replay/my_ft7/match.ttrm
node tools/finalize_tetrio_match.cjs /path/to/tetrio.js replay/my_ft7/match.ttrm replay/my_ft7_verified
```

第一个命令逐块比较完整棋盘（含隐藏区）、HOLD、攻击、已发送、消行、B2B、combo、
垃圾缓冲、洞口 RNG 和列位置；新采集版本另外比较 NEXT 队列及每块 Spin/Mini/消行种类。
完整文件还经官方客户端的 `ReplayFile.Update`/构造器导入路径验证多局容器，
检查对局列表需要的玩家身份与 APM/PPS/VS 字段；这不等于浏览器画面验收。
每个玩家流另外通过官方 `Preprocess('multi')`，检查播放器的时间轴、逐块跳转点和
快照预处理能够生成，并从最后一个快照实际跳转续播到结束、对比棋盘和统计；
离线环境只跳过呈现回调，不修改游戏引擎或输入处理代码。
第二个命令先验证原文件，再用**未修改的官方引擎**补全分数、finesse、spike 等附加统计，
保留 `native_stats` 和审计数据，再验证输出。失败时直接报错，绝不以替换棋盘“修复”差异。
满 7 胜输出新目录 `match.ttrm`，未满 7 的文件只能输出 `checkpoint.ttrm`。
这些可选工具需要 Node.js 18+；C++ 对战与普通 `.ttrm` 导出不依赖 Node/网络/官方 JS。

首个开发版运行期间修正了回放锁定相位和部分汇总统计；其现场对战、种子和落子没有改动。
仅对此开发捕获，补全工具可加 `--legacy-simultaneous`，将 P1 整体相位提前 7 帧；
HOLD 保持先于本块移动。原文件保留，转换前后均逐块验证。
当前 `tetr_ft7` 新采集的文件**不要加此参数**。

首个开发捕获还缺少主项目实际踢墙表。第七局 P2 第 1008 块的 `Z l s X Z H`
暴露此差异：原项目为 I-spin double，直接按官方默认 SRS+ 回放则落点错误。
现在用当前 C++ catalog 导出规则，并将同一规则事件补入旧捕获的每局开头：

```sh
./build/tetr_ft7 --rotation-profile > runlogs/ft7_dev1/native_rotation_profile.json
node tools/finalize_tetrio_match.cjs /path/to/tetrio.js runlogs/ft7_dev1/match.ttrm replay/ft7_verified --legacy-simultaneous --native-rotation-profile runlogs/ft7_dev1/native_rotation_profile.json
```

补规则后先与未改动的原始审计逐块对比，全部通过才发布。原文件保持原样，
输出 provenance 记录规则文件和原文件 SHA-256。新版本已自动记录规则，不需要迁移参数。

### 本轮新增

- `include/tetr/replay/tetrio_inputs.h`、`src/replay/tetrio_inputs.cpp`：
  独立的 TETR.IO 按键编码器，输入是已执行命令及其真实行列位移。
- `tests/tetrio_inputs_test.cpp`：编码与排序检查、帧边界、无效输入拒绝、
  30,000 次连续脉冲，以及由主项目真实移动/旋转/Spin 判定生成的 TSD 测例。
- `tools/tetrio_engine_reference.cjs`：离线官方引擎验证工具。
  不包含官方引擎源码，不加载 UI、网络、登录；使用调用者提供的官方客户端 JS。
- `tests/tetrio_inputs_reference_test.cjs`：用 C++ 编码器输出的事件，
  在官方 v19 引擎上执行 10 项兼容性检查，包括 I-spin 的默认规则失败/原规则成功正反例。
- `tetrio_random_test`：随机序列、洞口消耗、所有 7:0 至 7:6 比分终止边界。
- `tetrio_round_test`：原主机完整自然结束单局；测试限块=1/限秒=0.01 仍不能截断；
  验证 recorder 卸除后的默认 RNG 恢复；实际垃圾服务部分/整包抵消和 8 行截断续包。
  使用简单 DropBot 加速夹具，不冒充 BOT 比赛成绩。
- 三个 CMake 测试在 Release 下显式取消 `NDEBUG`，避免断言被优化掉。
- `tetrio_ft7_cli`：参数解析、无符号种子边界、拒绝覆盖和拒绝继承加速时钟。

### 单格下移的处理依据

本轮验证的回放 profile 是 `g=0, gincrease=0, handling.sdf=20,
handling.may20g=false, handling.safelock=false`。
这是 **导出回放的输入重建参数**，不是修改主项目处理方式或 BOT 参数。

在官方 v19 的零重力逻辑中，正时间间隔的软降更新至少下移
`0.05 * sdf` 格；SDF=20 时正好一格。
因此 `S` 编码成同一帧内、间隔 0.0001 子帧的 `softDrop` 按下/松开。
其他按键均先松开软降再执行，避免旋转过程中继续下落。

- 同一子帧按下并松开：实际下移 **零格**，负向测试已捕获。
- 不直接使用 SDF=41：它有不同的重力/20G 语义，不能保证一格。
- `s` 按主项目实际下移的格数编码为多次一格脉冲，而不是硬降。
- 已触底时仍记录一次被阻挡的软降输入；验证不会消掉刚完成的 Spin。
- 软降脉冲不跨帧：否则帧结束时的额外下落更新可能多落一格。
- 墙移按真实水平位移展开为单次移动，避免 DAS/ARR 引入另一条路线。
- 硬降只在 placement hook 记录一次，脚本 `H` 不再重复编码，避免下一块被误锁。

专项 TSD 夹具：主项目 T 块从 `(row=17,col=4,rotation=1)` 执行
`S X S H`，第二个 S 已被地形阻挡，最终为非 Mini 的 T-Spin Double。
同一个 C++ 编码器产生的事件在官方引擎上也得到一个 TSD、两行、一个锁定块。

### 验证

```sh
cmake -S . -B build
cmake --build build --target tetrio_inputs_test -j 4
ctest --test-dir build --output-on-failure -j 4
node tests/tetrio_inputs_reference_test.cjs /path/to/tetrio.js ./build/tetrio_inputs_test
node tests/tetrio_match_reference_test.cjs /path/to/tetrio.js ./build/tetrio_round_fixture/fixture.ttrm
```

参考 JS 来自 <https://tetr.io/js/tetrio.js>。
本轮审核的 SHA-256：
`b310aaf13ac0d7ee73c87c58abcdec87e1267217e182c10f4c9cb40dc5d73c92`。
工具严格校验此散列；后续客户端下载有变化时需要重新审核，不能直接放宽校验。
本机临时参考文件位于 `/tmp/tetrio-replay-reference.I6zpnf/tetrio.js`，
它不属于仓库，也不应作为跨电脑长期依赖。

动作层官方引擎 10 项检查通过；完整单局夹具 24 次落子通过。
最新全量 CTest 和抢七结果见下方验收记录。
官方客户端 UI 目前没有可用浏览器，**没有做拖入客户端的人工视觉验收**。
不能将离线引擎验证写成人工客户端 UI 已验收。

## 后续风险与验收记录

- 最新 Release 全量 CTest：**66/66** 通过；官方引擎动作检查 **10/10** 通过；
  回放传输专项 **11 组**通过（含损坏输入/错误胜者拒绝、未满七拒绝、七胜后续局拒绝、
  元数据补全不改输入、输出目录覆盖拒绝、旧捕获规则迁移及重复规则拒绝）。
  最新 CTest 日志：`runlogs/ft7_acceptance_20260925.log`。
- 默认沙箱禁止 UDP 套接字时，两项原网络测试会失败；在允许本地套接字的环境复跑后全通过。
- 官方 bundle 升级时必须重新审核，尤其是 Spin、Clutch、垃圾成熟和 Opener Phase。
- 尚未专门覆盖“前 14 块已经放出 B2B charge、同时双倍抵消”的极端组合；
  官方分段 FightLines 与主项目聚合抵消可能不同，后续新增夹具时需保留该边界。
- 非获胜方可能在最后一次锁定后自然判为 `garbagesmash`，主机在下一次出生时才确认输；
  最后到达的攻击包可能只增加主机已接收统计。原始审计及客户端补全统计分别保留，不改胜负。

普通入口、用户设置文件、当前 BOT 策略和已有实验日志均应保持不变。

## 完整抢七验收结果

2026-09-25 22:38（Asia/Shanghai）自然结束，进程退出码 0；没有中止、重赛、限块或限时截断。
验收使用 `--headless`，原项目的对战循环与真实低 PPS 路径完整执行，但未进行 UI 视觉验收。
启动时故意带入 `TETR_TEST_MAX_PIECES=1`、`TETR_TEST_MAX_SECONDS=1`，确认此入口忽略测试上限。
双方 autoplay/simulation PPS 均为 4，turn piece 为 1。

| 局 | P1 块数 | P2 块数 | 胜者 | 累计 P1:P2 |
| --- | ---: | ---: | --- | --- |
| 1 | 392 | 391 | bot_chatgpt | 1:0 |
| 2 | 422 | 421 | bot_chatgpt | 2:0 |
| 3 | 478 | 477 | bot_chatgpt | 3:0 |
| 4 | 54 | 54 | zzztoj_botris | 3:1 |
| 5 | 2035 | 2034 | bot_chatgpt | 4:1 |
| 6 | 51 | 51 | zzztoj_botris | 4:2 |
| 7 | 1443 | 1443 | zzztoj_botris | 4:3 |
| 8 | 1087 | 1086 | bot_chatgpt | 5:3 |
| 9 | 234 | 233 | bot_chatgpt | 6:3 |
| 10 | 157 | 156 | bot_chatgpt | 7:3 |

最终文件大小 42,918,828 字节；SHA-256：
`b1468b32386a8fbf299184c16cb0a1e81a4e3597807fa34a6a5539f1298da2f3`。
`replay/ft7_20260925_verified/` 中只有一个 `.ttrm`，即完整 `match.ttrm`。
该目录的 `provenance.json` 记录原捕获和旋转规则 profile 的 SHA-256、官方参考引擎版本及迁移选项。
最终逐块核验日志：`runlogs/ft7_final_verify_20260925.log`。
所有赛果来自原主机；官方引擎只用于核验和附加统计补全，不决定胜负或生成 BOT 策略。
