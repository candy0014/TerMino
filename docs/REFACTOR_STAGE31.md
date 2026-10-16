# Stage 31：模式强类型、初始化去重与 Piece Catalog 边界

本阶段一次完成原计划 Stage 31、Stage 32，并提前完成 Stage 34 的生产代码访问边界。

## 1. 模式与 modifier 强类型

新增 `runtime/game_modes.h`，定义：

- `GameMode`
- `GarbageMode`
- `RotationSystem`
- `AutoplayMode`
- `ModifierMode`
- `AllSpinMode`
- `FoolMode`

`RuntimeConfig`、Rules 输入、Engine adapter、Renderer 和 App 均使用这些类型。历史 `user_config.txt`、Replay 文本与 BOT 接口仍保留整数格式，只在明确的边界函数中转换。

正常生产代码中已清除：

- `mode.model` / `garbage.model` 旧成员名；
- `garbageMode == 10/11`；
- QP modifier 与裸 `0/1/2` 的比较；
- `rotationSystem == 0..6`；
- `autoplay.mode != 0`。

## 2. 删除 InitState

Stage 30 的 `src/app/init.cpp` 有 634 行，并通过 40 余字段的 `InitState` 在 `UserConfig::Settings` 与 `RuntimeConfig` 之间复制状态。

现在改为两条直接路径：

```text
user_config.txt -> UserConfig::Settings -> normalize_user_config() -> RuntimeConfig
Replay text     -> decode_config_prefix()                         -> RuntimeConfig
```

新的 `runtime/config_normalization` 负责所有 clamp、Quick Play 默认值、modifier 联动、20G 与垃圾延迟。每次初始化均从新的 `RuntimeConfig` 开始，不再可能继承上局残留字段。

`init.cpp` 只保留应用层职责：加载 Replay、等待多人 Lobby、设置随机种子、初始化双方场地以及首屏绘制，行数降至 274。

## 3. Replay 配置 codec

新增 `replay/config_codec`：

- 集中保存历史 `{name,value}` 字段表；
- 保持已有字段名、顺序与数值文本格式；
- 未知字段继续忽略，保持前向兼容；
- 缺失括号、缺失分隔符、非法或溢出的 seed 会安全拒绝；
- App 不再通过裸指针 Node 表手工读写 Runtime 字段。

## 4. Piece Metadata

新增 `Piece::Metadata`，统一提供：

- 名称与显示宽度；
- 颜色；
- Preview/Hold 显示尺寸与偏移；
- 家族内编号；
- `Piece::SpinCheck`；
- `RotationSystem`。

`board_lifecycle`、`placement_coordinator` 与 `board_renderer` 已完全退出 `PPP::allblock_*`。裸 PPP 表访问只保留在 `runtime/piece.cpp` 的 catalog 实现、测试参考实现和 protected BOT 兼容区。

同时将误导性的 `cellCount / pieceCellCount` 更名为 `familyIndex / pieceFamilyIndex`。旧值实际是各旋转系统内的 piece 编号，并不是方块包含的格数。

## 5. 其他清理

- Linux `clear_screen()` 直接输出 ANSI 清屏序列，不再启动外部 `clear` 进程。
- `tools/ppp_generator.cpp` 删除 `bits/stdc++.h`、`using namespace std` 与未检查的 `freopen`，改为显式流和输入错误报告。
- Replay 模式不再生成无意义的单字符 `configPrefix`。

## 6. 回归验证

- Debug：33 / 33 PASS。
- Release（测试显式 `-UNDEBUG`）：33 / 33 PASS。
- `tetr`、`tetr_server`、`tetr_ppp_generator` 均完成 Debug/Release 链接。
- 86 / 86 个非 BOT public header 独立编译通过。
- 非 BOT 生产源码与 PPP 工具在 `-Wall -Wextra -Wpedantic -Werror` 下 0 warning。
- 8 / 8 个 protected BOT header 与 Stage 30 SHA-256 完全一致。

当前验证环境未提供 CMake，因此使用与项目依赖关系等价的 `g++` 静态库链接方式完成上述两套全量构建和测试；CMake target/source/test 清单已同步更新。
