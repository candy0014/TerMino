# bot_chatgpt

主游戏通过 `src/bot/bot_chatgpt.cpp` 接入搜索与评估，复用 `zzztoj_standalone/upstream` 的树与地形基础设施。默认关闭所有 `TETR_CHATGPT_*` 实验开关；`CMakePresets.json` 提供明确的稳定配置和独立候选配置。

保留实际构建使用的模型、搜索代码、基准与报告工具，未附带个人对局、历史研究日志或冻结二进制。实验开关不能视为默认策略或棋力保证；部分开关互斥，CMake 会检查组合。

上游来源和边界修复见 [集成说明](../zzztoj/UPSTREAM.md)；BOT 规则支持范围见 [兼容说明](../../../docs/BOT_COMPATIBILITY.md)。基准工具 `benchmark.py` 使用主游戏可执行文件，运行输出应放到 Git 忽略的 `runlogs/` 目录。
