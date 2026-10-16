# Cold Clear C++ 移植

来源：[MinusKelvin/cold-clear](https://github.com/MinusKelvin/cold-clear)，固定版本
`279edd7c3177ff8077f6a930193397814b281f27`。完整原始 Rust 源码和 MPL-2.0
许可证保存在 `third_party/cold-clear/`。新增 C++ 移植文件同样遵循 MPL-2.0。
游戏通过 CMake 编译 C++17，没有 Rust、Cargo、动态库或外部 bot 进程依赖。

在 `Settings → AUTOPLAY → Bot` 中选择 `bot_coldclear`。配置文件中的
`Autoplay` 值为 `10`；历史回放格式使用独立兼容编号 `13`。

## 移植范围

| 原始 Rust 模块 | C++ 文件 | 实现 |
| --- | --- | --- |
| `bot/src/dag.rs` | `src/bot/coldclear/search.cpp` | DAG 状态去重、多父节点回传、按评分和排名加权的随机分支选择、未知方块的机会节点与期望值、死亡分支惩罚 |
| `bot/src/modes/normal.rs` | `search.cpp`、`board.cpp` | 当前方块与 HOLD 分支、空 HOLD 的额外队列消耗、生成可达落点、预测锁定结果 |
| `bot/src/evaluation/standard.rs` | `evaluation.cpp` | Standard 默认权重、凹洞/悬空/覆盖深度、井深/井列、行转换、地形起伏、六类 T-slot 与 cutout、Value/Reward/spike 算法 |
| `libtetris` | `board.cpp` | 底部为原点的紧凑棋盘；几何、踢墙、重力和攻击改用本项目规则 |

当前接入 Normal 搜索模式。上游 opening book、专用 PC-loop 求解模式、
异步后台线程和跨落子 DAG 复用没有接入；全清仍会被正常搜索识别并评分。
每次根据公开棋盘、HOLD、NEXT、可见袋余量和待收垃圾构建搜索，避免跨玩家、
重开和 Undo 时持有旧状态。随机采样种子由公开棋盘与落子数生成。
这不是与上游逐步决策完全相同的实现，也没有声称达到上游原版棋力。

## 宿主规则适配

- 使用本项目的方块 catalog 和踢墙表，覆盖 SRS+、180° 旋转及其他标准七块
  旋转系统；ARS 的中心格检查与引擎一致。棋盘保留全部 100 行隐藏区，支持
  Clutch Clear 的抬升生成。最终目标由公共 bot adapter 再做移动脚本检查。
- 可达性使用 `Engine::apply_movement_script_command`，支持普通/关闭/20G
  重力、DAS=0、ARR=0、禁用单项移动、软降和旋转。
- Spin、全清、连击、B2B 充能释放、Garbage Special Bonus 和发出倍率通过
  `Rules::resolve_placement` 计算。保留 Standard 的消除效率偏好，以每行
  150 分的原连击垃圾权重修正 PPT 与项目实际伤害的差值。
- 待收垃圾使用项目的延迟、FIFO、涨行上限、开局双抵消和小数抵消余量。
  清行会延缓涨行；洞位尚未公开时在该分支的首次涨行处停止继续搜索，按
  抬高后的危险高度评分，不猜测私有随机洞位。
- 七袋尾部按公开袋余量展开，其他随机器按七种方块等概率近似。NEXT=0
  时根节点不会通过空 HOLD 选择不可见方块。Quick Play 的随机攻击取整按
  确定性向下取整评估，实际引擎仍使用其原有随机流。

要求 10×20 和七种标准 tetromino，最多一次 HOLD。反重力、异形方块、
固体垃圾变换、Quick Play 的重力进阶及改变场地的特殊修改器会在菜单显示
不可用原因。普通 Quick Play 的 Spin/攻击修改器使用宿主规则。

`SearchWidth` 是搜索质量参数：1..1000，节点预算为 `max(256,24×width)`，
最多 `8×width` 次采样，深度上限为可见方块数加二且不超过 16。
完整机会节点展开可能超过节点预算。默认 300；减小数值可以减少 CPU 时间。

## 验证

`bot_coldclear_test` 包含：

- 696 组原 Rust Standard 评估器地形/T-slot 参考值逐项对照。
- 八种标准旋转系统的新旧几何与引擎碰撞、踢墙结果对照。
- 不同 Spin 模式、连击、B2B 状态、TSD、Quad、全清的引擎结果对照。
- 20G、DAS=0、禁用移动、已满/空/禁用 HOLD 与 NEXT=0 的可执行脚本核对。
- 七袋机会节点、DAG 去重、垃圾延迟与上限、开局和小数抵消核对。
- 固定七袋连续落子，逐块执行移动脚本并与引擎的 Spin、清行和攻击核对。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target tetr bot_coldclear_test -j2
ctest --test-dir build --output-on-failure -R 'bot_coldclear|bot_selection'
./build/bot_coldclear_test 300 300 279
```

重新生成地形参考值可运行 `python3 tools/coldclear_reference.py`。只有该
离线生成步骤需要 `rustc`；它在临时目录运行下载的原始 `standard.rs`，
输出已保存的 CSV；日常 C++ 构建与测试只读取 CSV。

2026-10-07 验证记录：Release 全部 99 项回归通过；其中两个本地套接字测试
因沙箱禁止网络而在沙箱外重跑通过。实际 `build/tetr` 的宽度 100 冒烟测试
完成 Zen 120 块（45 行）、Cheese Layer 120 块（消除 43 行垃圾），本地对战
while1 正常运行至自然结束，并保存了配置编号 13 的原生回放。本次单局对战
Cold Clear 在 60 块时输给 while1，仅作为接入验证，不能据此推断总体棋力。
日志、汇总和回放保存在 `runlogs/coldclear_20261007/`。
