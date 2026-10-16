# 2026-09-27：蓄攻与短期修洞研究

本轮持续**2小时57分44秒**，两小时研究要求已完成，**对Botris和while1各前9自然局至少5胜的目标未达到**。当前默认程序保持原版，29个实验全OFF。

|方案|Botris胜/9|while1胜/9|
|---|---:|---:|
|原版重复对照 (`baseline_repeat_v1`)|3/9|2/9|
|连续消行只计峰值风险 (`clear_burst_v1`)|1/9|2/9|
|两步连消修洞证明 (`clear_recovery_v1`)|0/9|3/9|
|允许准备一步的修洞证明 (`setup_recovery_v1`)|1/9|2/9|
|已可释放的B2B储备估值 (`charge_reserve_v1`)|3/9|5/9|
|储备估值＋连消风险 (`charge_burst_v1`)|0/9|5/9|
|按出生区域估计压力 (`spawn_columns_v1`)|3/9|未启动：Botris门槛未过|
|取消实际攻击时间折扣 (`flat_attack_v1`)|2/9|未启动：Botris门槛未过|
|三块净输出集中度 (`burst_window_v1`)|2/9|未启动：Botris门槛未过|

本轮135个自然结束局、1个块数截断、1次保留的无效中断尝试，所有已启动组完成。开发种子未替换，预留种子未使用。32257次现场决策的输入与实际转移审计通过，零回退。原版重复对照没有重置历史资格。

Moch会保留现成攻击再准备并集中释放；#425同终点并不表示战略等价。#410显示最终主动释放B2B也可能是搭建的一部分。与此同时，更新输入后的#393原版也会接垃圾、延后T攻击，差异需精确到落点与释放时机。

新增64段来自额外48局/12文件，交互页面共457根×9模型；两边完整5步、消行前后、B2B/combo/atk和每步评分均可查看。另补8段各10块上下文；首根可复算路线不意味着Moch后续决策没有新信息。

- [完整结果与边界](../runlogs/chatgpt_recovery_20260927/RESULTS.md)
- [五步对照页面](../runlogs/chatgpt_recovery_20260927/all_root_cases.html)
- [蓄攻时序分析](../runlogs/chatgpt_recovery_20260927/ATTACK_TIMING.md)
- [后续上下文与重规划](../runlogs/chatgpt_recovery_20260927/FOLLOWUP_OBSERVATIONS.md)
- [最终交接](../runlogs/chatgpt_recovery_20260927/HANDOFF.md)


最终耗时补注：32257条搜索中只有1条超过120ms，来自原版重复while1首局73004001、行动前pieces=12，记录942.00325ms，日志第41行。该根roof=5、incoming=0、fallback=0；不是修洞候选。未记录每轮CPU时间或调度轨迹，具体来源不能确定，不能称全部搜索满足严格100ms墙钟硬上限。保留原局，不换种子、不用重跑替换；详见[耗时异常记录](../runlogs/chatgpt_recovery_20260927/timing_outliers.json)。
