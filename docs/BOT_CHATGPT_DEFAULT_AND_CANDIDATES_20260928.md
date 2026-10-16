# bot_chatgpt 默认回退版本与候选策略详解（中文）

整理日期：2026-09-28。本文依据本仓库实际源码、冻结产物与已有主程序对战记录编写，覆盖默认版本的完整决策流程，以及搜索覆盖、状态去重、地形优化、B2B 储备、攻击兑现、袋尾续算、材料储备、关键块和可达空洞等代表候选。末尾另列全部 38 个仍保留的构建选项及历史归档入口。

## 1. 默认回退到哪一版，以及“最强”的证据边界

默认采用 **阶段 5 模型修正版 F，`bot_chatgpt v0.3.2-rc1` 的现有宿主集成版本**。其固定权重为攻击 560、末端 B2B 480、挖原垃圾 240，使用原生渐进加宽搜索。宽度 300 对应名义 100 ms，全部 38 个 `TETR_CHATGPT_*` 实验关闭。

默认程序为 `build/tetr`，SHA256：

```text
c91106e0cdc7904170071889c64c0372588cf46e8538a9c547d56b72d4341f7a
```

这次检查发现默认程序原本已经恢复到这套策略，因此实际交付是**核验并重新应用稳定配置、固定可重复的回退入口**。没有用几天前的整个仓库快照覆盖当前游戏。阶段 5 F 的早期二进制哈希 `c53f70a0…9f48`、阶段 8 只读诊断版 `689dc899…d922` 与现在的宿主集成版属于不同构建；不能因为策略名称相同，就把源码来源和程序哈希写成同一份。

“目前最强”的操作依据是：保留已有综合对战证据支持的默认基线；后续候选需有足以支持替换默认的综合证据。**现有数据不能严格证明某个版本在全部对手、全部种子上总体最强**。部分候选的某批开发胜局确实更多，本文如实列出；没有把它们隐去，也没有将“未通过晋级”写成“所有样本都更弱”。

| 比较批次 | F 策略基线 | 对照候选 | 选择依据 |
|---|---|---|---|
| 阶段 5，新种子、额度 1:1.3，历史 FT3 | 对 while1 为 2:3；旧版 v0.3.1 为 0:3；Botris 两版均 1:3 | F 相对旧维护版的有限改善 | F 作为后续基线；两个系列仍输，不能宣称总体优势已证明 |
| P5 完整入口覆盖，20 个系列/版 | 19 胜 57 负，25.00% | 11 胜 60 负，15.49% | 原版点估计较好；差值区间跨 0，候选没有通过替换门槛 |
| 2026-09-27 状态去重开发组，两个对手各 9 自然局 | Botris 2/9、while1 1/9 | 两对手均 3/9 | 去重有开发改善，但未达双对手门槛、未用独立确认组 |
| 2026-09-27 B2B 储备估值开发组 | 同期重复基线 Botris 3/9、while1 2/9 | 储备估值 3/9、5/9 | while1 改善值得保留，但 Botris 未达门槛，尚无独立确认 |
| 普通 T 兑现选项 v3，开发组 | 基线见原批次，不能用其他批次代替 | Botris 3/9、while1 5/9 | 单个对手过门槛，双对手未过；保留为重点候选 |
| 旧关键块 v3，同批各 9 自然局 | Botris 2/9、while1 3/9 | 2/9、2/9 | 没有综合强度改善 |
| 完整可达空洞旧版，Botris 1:1.3，20 局/版 | 5 胜 15 负 | 0 胜 20 负 | 同批明显退步，保留记录 |
| 加速与半权重空洞版，新种子 20 局/版 | 2 胜 18 负 | 3 胜 17 负 | 候选胜局多 1，但平均存活、攻击和挖掘更低，小样本不足以支持替换 |
| 历史保 B2B 挖掘 v5，各对手 9 自然局 | 见该批原始报告 | Botris 0/9、while1 0/9 | 没有强度依据；实现已按此前要求移出当前构建，归档保留 |

FT3、固定 10 次尝试、前 9 个自然结束局和固定 20 次尝试是不同协议。截断、人工中止和自然结束分别保留；不同批次不直接合并计算“最强胜率”。同种子也不能消除计时搜索、局面分化和对手响应的影响。

依据：[阶段与晋级日志](BOT_CHATGPT_LOG.md)、[P5 群体结果](BOT_CHATGPT_LOG.md#p5-a完整入口移动覆盖与扩大群体测试已完成不晋级)、[9 局候选结果](BOT_CHATGPT_GOAL_20260927.md)、[储备实验结果](../runlogs/chatgpt_recovery_20260927/RESULTS.md)、[兑现候选结果](BOT_CHATGPT_SEGMENTS_20260927.md)、[关键块比较](BOT_CHATGPT_KEY_PIECE_COMPARE_20260927.md)、[半权重空洞结果](../runlogs/chatgpt_holes_optimized_20260928/REPORT.md)。

## 2. 默认版本的输入、状态和信息边界

入口是 [bot_chatgpt.cpp](../src/bot/bot_chatgpt.cpp)。每次真实落块前，根据宿主提供的信息建立新根状态，搜索后仅执行第一步，下一块重新决策。

它读取以下信息：

- 当前 10 列场地，包括可见 20 行与顶部隐藏空间；内部使用 40 行位棋盘，内部行号从底部向上递增。
- 当前块与公开 NEXT，实际活动块的行、列、方向，HOLD 内容和本次是否允许 HOLD。
- 当前 B2B 整数、combo、已放块数、上一块是否消行、原有垃圾高度。
- 已经可见的垃圾缓冲 FIFO：各包行数、距离就绪的时间；游戏当前时间、垃圾延迟和每块推进时间。
- 公开规则开关，例如开局双倍抵消是否启用。

原版不把未公开方块顺序、未来对手攻击或未来垃圾洞位加入搜索。宿主可能传入公开袋内余块集合，原版没有使用该集合推断额外 NEXT；袋尾与关键块候选才有相应逻辑。根诊断记录袋信息是为了复查输入，不能据此说原版利用了袋尾。

支持范围来自共享宿主适配器：10×20 标准场地、SRS+、普通重力、非随机出块、至少一个 NEXT，且不启用适配器不支持的 Quick Play 特殊修饰。bot_chatgpt 还要求重力间隔大于 `1e-7`，不属于瞬时重力解法。这里记录的是实现边界，不意味着适用于所有自定义玩法。

每条分支携带 `combo / b2b / garbageHeight / incoming / readyIncoming / risen / previousCleared / placedPieces / dead`，以及累计 `attack / reward / pathRisk` 和末端 `value`。`attack` 是模型内结算的攻击总数，`value` 是用来排序的启发分数；二者不能互换。

## 3. 默认版本怎样搜索：落点枚举、渐进加宽与重新规划

### 3.1 方块的移动和合法落点

[search.h](../src/bot/chatgpt/search.h) 使用 zzztoj 的预计算 SRS+ 几何图。节点包含方块种类、位置和方向；边包含下移、左右平移、顺转、逆转和 180° 转。

搜索先尝试沿当前列快速下降到堆顶附近，再从这个起点遍历移动图。每次旋转按照实际踢墙列表顺序试探，**只使用第一个成功的 kick**。能够放得下的最终形状仍须有路径；最终位置不能继续下移时才作为锁定落点。

末步如果是平移或下移，旋转标志清零；末步为旋转时保留 kick 索引，并交给宿主规则判断 full Spin / Mini。几何相同的多个路径默认只保留较强 Spin 标签，优先级为 full Spin、Mini、普通落点。这减少分支，但可能抹掉“主动普通消行断 B2B”的选择，详见兑现候选。

**已知限制**：快速下降不是完整入口移动枚举，可能漏掉必须在高空提前横移或旋转的合法落点。阶段 6、P5 和仅根覆盖候选都研究过此问题。原版通常输出合法首步，但“没有输出非法落点”不等于“枚举了所有合法落点”。

### 3.2 树搜索与工作分配

默认引擎是 `TetrisEngine<TetrisRule, Evaluation, Search>`，复用上游公开树节点、等待堆和已展开堆。它不是只看一块的贪心，也不是固定展开所有路线的穷举。

每轮扩大搜索宽度：第一次生成根的子节点，之后每轮将内部宽度加一。在每个剩余深度层，优先展开分数较好的待选节点。评分器提供 `ratio() = 1.5`；层配额按剩余深度的 `pow(index+2, 1.5)` 曲线及当前宽度计算，至少展开一个。实际搜索量取决于分支数、逐点评分成本、预览和时间。

最终 `get_best()` 从较深的非空层开始，在该层的待选和已展开堆顶之间选更高分，再沿父指针取根的首步。因此，页面中的五步/六步前缀可能不是完整计划，局部六步分数也不一定等于最终选择依据。HOLD 和公开序列长度会改变有效深度；不能把它描述为永远固定深度 6。

### 3.3 时间、内存、树复用

预算公式为：

```text
budget_ms = clamp(autoplaySearchWidth, 3, 300) / 3  （整数除法）
```

宽度 300 请求 100 ms，150 请求 50 ms，60 请求 20 ms。较早报告曾使用不同公式，应按对应冻结版本理解。当前“Width”主要控制时间，不是固定保留 300 个节点。

原生计时接口在一次完整迭代结束后检查时钟，初始化和单轮计算也占时间；单轮不可随时中断，因此 100 ms 不是硬墙钟上限。宿主首步路径校验还有额外开销，诊断的 `searchMilliseconds` 不包括随后这段验证。

宿主设置 `memory_limit(64 MiB)`，但当前调用的上游计时重载没有以此作为循环停止条件，不能称全过程受严格 64 MiB 限制。它也不是整个程序的总内存限额。

搜索工作区为 `thread_local`，每次 `init()` 重置，后续真实落块可复用树分配与几何。每个真实根重新传入 B2B、垃圾时钟等状态并调用 `update()` 强制刷新评分。首次有效搜索前不调用尚未初始化预览长度的 `update()`，这是 v0.3.1 开始保留的初始化修复。不同线程不会共用同一评分根；实际使用不是缓存旧分数直接出招。

## 4. 默认版本的 Spin、消行、B2B、combo 与攻击

这是分支结算规则，是评分的输入。来源为 [spin.cpp](../src/rules/spin.cpp)、[chain.cpp](../src/rules/chain.cpp)、[attack.cpp](../src/rules/attack.cpp)，bot_chatgpt 调用同一组规则函数。

### 4.1 Spin 判定

T 使用三角判定：有最后旋转记录，并检查中心四角、朝向前两角和 kick。符合 full Spin 条件时按 full 处理，否则满足 Mini 条件时按 Mini。非 T 使用不可移动判定：末步是旋转，且不能上移、左移、右移；标准配置下属于 Mini。不能仅凭棋盘看起来像槽就判 Spin。

### 4.2 连链与充能释放

- 零消：B2B 保持原值，combo 清零；零行 Spin 不增加普通规则下的 B2B。
- 四消、Spin 消行或全清：B2B 增一；消行时 combo 增一。
- 普通一至三消：B2B 断为 0；若旧 B2B 至少 5，则释放 `旧 B2B−1` 的充能攻击；这次消行仍延续 combo。

这里的 B2B 是完整链整数，不是简单布尔值。主动普通消行有时是兑现储备的合法策略，原版没有绝对禁止断链。

### 4.3 标准攻击公式

先计算基础攻击 `basic`：普通一/二/三/四消分别为 0/1/2/4；full Spin 一/二/三消为 2/4/6，四消为 10；Mini 使用普通基础表。当前 B2B 至少 2 时，等级加成为 1，否则为 0。

```text
attack = floor((basic + b2bLevel) × (1 + 0.25 × (max(combo,1)−1)))
```

若 `basic` 和 B2B 等级均为 0，则使用普通单消连击公式 `floor(log(1 + 1.25 × (max(combo,1)−1)))`。然后加全清 5、合法 B2B 充能释放，以及符合条件的垃圾特殊消行奖励 1。垃圾奖励要求四消或 Spin 消到了至少一行垃圾，是每次事件加 1，不是每行加 1。零消不发攻击。

原生评分器使用标准配置的攻击输入。Quick Play 或其他修饰的扩展规则不是本文默认对战的策略依据。

## 5. 默认版本的完整地形评价

默认地形取 [上游 `Botris::eval`](../zzztoj_standalone/upstream/src/ai_zzz.cpp)，但 bot_chatgpt 的攻击、垃圾、链和风险评分独立实现；它没有调用对手的整套出招策略。有效地形分数为：

```text
T = −128 × 场高
    −160 × ColTrans
    −160 × RowTrans
    −380 × 含洞行数
    −100 × 井深累计
    − 40 × 连续洞深累计
    − Σ[32 × 0.25^i × ClearWidth_i]
```

各项的实际定义如下，不能简单用“平整度”代替全部内容：

| 项 | 实际计算 | 策略含义 |
|---|---|---|
| 场高 | 所有列最高占用格之上的高度 | 降低高堆成本 |
| `ColTrans` | 代码名为 ColTrans，主要累加每行左右空实变化、两侧边界及空上部常量 | 避免表面与内部过于碎裂 |
| `RowTrans` | 代码名为 RowTrans，主要累加相邻行位模式变化，含上下边界 | 避免竖向碎裂；注意名字与直觉中的方向不一致 |
| 含洞行数 | 从顶往下累计上方覆盖掩码，该行被覆盖的空格非零就计一行 | 同行多个洞在此项只计一次 |
| 连续洞深累计 | 同列连续覆盖空格长度依次加 1、2、3…，遇非洞重置 | 长洞串罚分随长度加速增长 |
| 井深累计 | 累计覆盖掩码中两边占用、中间未覆盖的井；两侧墙作为边界 | 深井有成本；普通版没有完全豁免主攻击井 |
| 覆盖清除成本 | 对第 i 个含洞行向上扫描，计算与洞列相交实格的 `(上方内部行号+1) × 格数`，遇整行无交集就停止；乘 `32 × 0.25^i` | 上层洞的上盖代价更大；包含绝对高度而非纯局部盖深 |

`i` 按从高到低遇到的含洞行编号，首行为 0。井计数器的原实现不在其他行重置；不能自行换成另一种“连续井”定义后称分数不变。

上游还写有 `−80 × HoleCount`，但当前 `Botris::eval` 没有累加这个局部计数，实际为零。**原版并非没有洞罚**：含洞行、连续洞深、覆盖清除成本都在起作用。阶段 8 额外每洞罚 80 的候选被拒绝，原版额外 `coveredEmptyWeight = 0`。

空棋盘单独返回 `−16000`，避免边界访问；全清奖励由实际规则与累计收益给出，不能把这个负常量解释为“原版反对全清”。地形是每个末端重算的状态价值，不把每步地形分再累加。

## 6. 默认版本的累计收益、末端价值与路径风险

令第 d 次分支放置的折扣 `q_d = 0.96^d`，根的首步 d=0。每步真实模型收益为：

```text
ΔR_d = q_d × (560 × attack_d + 240 × garbageCleared_d − 40 × lines_d)
```

普通 T 使用且非全清再扣 `140 × q_d`；I 一至三消且非全清再扣 `70 × q_d`。T 的扣分判断是 `!spin`，不仅仅检查这块有没有消行，所以用 T 作普通补形也会付成本。I 的扣分只针对一至三消，不针对普通无消行铺垫。

`garbageCleared` 只统计原已知底部垃圾区域中的消行掩码，并相应减小 `garbageHeight`。原版未知上涨采用高度偏移，不能凭空获得挖掉这些新未知垃圾的奖励。

节点危险高度与路径风险为：

```text
D_d = max(0, roof_d + risen_d + min(8, incoming_d) − 12)
P_d = P_(d−1) + q_d × 35 × D_d²
```

末端分数为：

```text
V_d = T(board_d) + R_d
      + 480 × B2B_d + (B2B_d > 0 ? 300 : 0)
      + (HOLD 为 T 或 I ? 80 : 0)
      − 95 × D_d² − P_d
```

预测死亡再扣十亿，之后进入吸收态：后续虚拟放块不再增加攻击、收益或重写首次死亡分数。所有路线都差时仍按现有分数选择，并没有已启用的“死得更晚额外加分”。

默认末端 B2B **没有按深度折扣**。攻击收益按深度折扣，而储备每单位值 480；这会影响“现在释放”与“继续蓄链”的相对价格。攻击数相同的路线还会受到挖垃圾、用块代价、末端地形与累计风险影响。

`comboContinuation` 默认是 0，故不存在额外 combo 储备项。combo 的真实攻击倍率继续生效。`experimental_policy_for()` 留有受压快挖权重混合，但入口使用固定 `PolicyWeights{}`，没有启用这种自动风格切换。

这个风险模型同时罚末端和沿途：避免最后降得很低就隐藏中间危险。其代价是，即使连续消行阻止了垃圾上涨，仍可能反复对尚在缓冲的垃圾收风险费；后续风险候选正是研究这个取舍。

## 7. 默认版本的可见垃圾预测、抵消和上涨

[forecast.h](../src/bot/chatgpt/forecast.h) 冻结本根已可见 FIFO 包的相对到时，以 `d × secondsPerPiece` 估算分支中的就绪状态。`secondsPerPiece` 优先取宿主传入值；不可用时用预算毫秒换算。额度模式每块游戏时间与实际 CPU 墙钟耗时不是一回事。

每步结算顺序：

1. 更新分支已放块数，并从可见时序计算就绪 FIFO 前缀。
2. 按真实消行、Spin、B2B、combo、全清和原垃圾消行数计算攻击。
3. 抵消缓冲垃圾。开局双抵消在宿主允许、放块数不超过 14、攻击正数且待接垃圾大于攻击时启用；抵消预算为攻击的两倍，其他情况为一倍。
4. 重新确认消耗 FIFO 前缀后哪些剩余包已就绪。取消未到时的前包，可能暴露其后已经到时的包，不能缓存原前缀的最大到时替代此计算。
5. 当前块消行时暂停上涨；不消行时就绪前缀至多上涨 8 行，并从 `incoming / readyIncoming` 移除，累计到 `risen`。

`risen` 是未知垃圾导致的垂直偏移，没有在评分用棋盘里填入假设洞。出生安全检查把偏移下方视为保守未知基底。这个模型能表示压力提高和部分出生风险，**不能完整表示未知洞位下的可达路径、挖掘或恢复路线**。后续在相对棋盘上算到的收益可能有模型误差；这是独立局限，不是先知地知道垃圾洞。

根之后新发出的对手攻击没有被预测；后来真实到来的包由下次根重新读入。原版不是双方交替 minimax，也没有按对手 ID 切换出招。

## 8. HOLD、出生、Clutch、合法性校验与回退

搜索器把当前块、NEXT 游标与 HOLD 一起维护。空 HOLD 会消耗首个 NEXT，非空 HOLD 交换已有块；禁止 HOLD 时不启动对应分支。末端 HOLD 是 T 或 I 获得 80 的固定储备价值，不区分槽、袋内等待或来袭。

出生优先正常 SRS+ 出生点；被挡时，仅在上一块真实/模型消行允许 Clutch 的条件下，尝试更高出生点。HOLD 的出生检查使用**该候选之前**的消行状态，不能用候选自己将要消的行提前取得 Clutch。宿主是出生死亡规则，不是所有超出可见 20 行的锁定都无条件死亡。

选中路线后，宿主独立检查块型是否来自当前/NEXT/HOLD，并用主项目实际碰撞、旋转和 Spin/Mini 路径重新验证首步。HOLD 返回协议靠种类变化表示交换；同种类 HOLD 不能假定宿主会重置出生点，实际起点仍须保留。

原版还保留空 HOLD 的即时评分预览上下文限制：原生树后续推进正确，但当前候选的 `env.next` 可能仍指向被空 HOLD 消耗的块。P3-A 修正过这一问题，未通过强度筛选，因此没有隐式装入默认。预览耗尽后、已知 HOLD 与未知活动块之间的尾部模型也不等于完整随机未来搜索。

路径校验失败或搜索没有合格目标时，使用宿主 `safe_fallback`：从**当前真实块**的实际位置枚举可达锁定点，比较消行后的洞、高度、总列高与起伏。回退成本越低越好：

```text
cost = 100000 × 洞数 + 5000 × 最高列高
       + 200 × 总列高 + 100 × 相邻列高差总和 − 10000 × 消行数
```

它不搜索 HOLD，也不使用完整 B2B/攻击规划，是异常情况下的合法出口。不能用回退输出宣称原搜索已经证明这条路线强。`fallbackReason = 0` 才表示首步成功通过正常路径；历史对战中很多批次记录为零回退。

## 9. 候选的共同继承规则

下文明确选入详解的候选包括 P5/仅根覆盖、去重、精确地形、风险修正、B2B 储备/储备加连消、普通 T 兑现、袋尾、总材料/己方材料、地形 V1/V2、关键块 v3、可达空洞完整版与优化半权重版，以及已归档保链 v5。

除各节列明改动外，它们继承第 2–8 节的输入边界、标准规则攻击、可见 FIFO、出生和宿主首步验证。额外值通常是终端排序信用，不能加入真实 attack/reward。构建是一次启用一个代表策略；某些候选自身包含明示的组合，例如储备加连消，但不表示可以任意叠加全部 CMake 开关。冻结程序与当前头文件必须分清：旧 `key_piece_v3` 的成绩属于其旧源码，新 KEY_PIECE 构建已是半权重空洞版本。

## 10. 完整入口覆盖 P5-A 与仅当前根完整覆盖

### 10.1 P5-A：所有深度都从实际入口开始

开关 `TETR_CHATGPT_P5_A`；实现 [coverage_search.h](../src/bot/chatgpt/coverage_search.h)。它取消默认枚举器的提前下降，从真实当前位置或合法出生位置遍历完整移动图，允许高空横移/旋转后进入塔顶与侧面。

它继续使用首个成功的 SRS+ kick、每个几何位置较强 Spin 去重、旧评分与同一时间预算。改变的是可供搜索的落点集合，不是攻击或 B2B 加奖。2307 个落点的定向核查与完整移动图一致，补回了原枚举遗漏；但每次枚举更贵，同样 100 ms 内可能少展开后续分支。

较大群体测试中，基线 19/76 胜、候选 11/71 胜，均为 20 个 FT3 系列；分母因各系列结束长度不同而不同。候选减基线为 −9.51 个百分点，整系列 bootstrap 区间约 [−22.31,+2.95]，未取得提升证据。单人挖掘局部改善不能替代对战。归档位于 `runlogs/chatgpt_plan_p5_candidate_a/`。

### 10.2 RootCoverage：把完整覆盖成本限制在真实根

开关 `TETR_CHATGPT_ROOT_COVERAGE`；实现 [root_coverage_engine.h](../src/bot/chatgpt/root_coverage_engine.h)。深层继续原快速枚举，只有当前真实根完整覆盖。

它处理树复用的重要细节：原深层节点成为新根时，旧节点可能已有不完整子表。新根版本改变时强制合并完整根落点，对已有几何位置重新刷新 Spin/Mini/kick，再评分，而不是发现“已经有子节点”就跳过。

评分与渐进加宽规则继承原版，不加入普通 T 兑现选项。开发对 Botris 1/9、while1 3/9，同期基线 2/9、1/9；一个对手改善、另一个下降，未通过综合门槛。完整根覆盖只证明当前候选集合较完整，不证明之后未知垃圾下的整条路线可执行。

## 11. 状态去重 Transposition

开关 `TETR_CHATGPT_TRANSPOSITION`；实现 [transposition_engine.h](../src/bot/chatgpt/transposition_engine.h)。评分、移动枚举和层配额沿用默认；在准备展开节点时，查 4096 槽表，跳过已被等价状态支配的路线。

键由完整 40 行棋盘，以及深度、NEXT 游标、HOLD、combo、B2B、原垃圾高度、剩余/就绪垃圾、上涨偏移、前次消行、已放块数组成。HOLD 和游标从父节点按真实消耗推导，避免尚未展开的懒初始化字段污染键。根版本变化或新决策更新时清代际，表冲突还要比较完整键。

支配条件是已有等价节点的累计奖励不少于当前节点，且累计风险不大于当前节点。只比较一个合成净分可能引入舍入或不完整状态问题，所以实现保留分量判断。发生槽冲突或奖励/风险互不支配时不会证明性删除路线。

它没有改成全局无限精确的动态规划：表有界、有覆盖替换，原落点缺失和未知上涨近似仍在。开发两对手均 3/9，相对同期原版 2/9、1/9有所改善；未达双对手门槛、未进入独立确认。候选保留，默认关闭。

## 12. 精确等价地形 FastTerrain

开关 `TETR_CHATGPT_FAST_TERRAIN`；实现 [fast_terrain_evaluation.h](../src/bot/chatgpt/fast_terrain_evaluation.h)。这是计算优化：保留第 5 节每项数值，包括历史 `HoleCount=0`、绝对上盖行权重、空板 −16000、井计数语义。

它用位操作稀疏遍历覆盖洞，按各列顶高直接算井的三角累计，避免逐列逐格重复扫描。`clearedRows`、攻击和风险仍使用原实现。改变的是单位节点评价速度，不是新风格或新洞权重。

10000 张随机板、5266 张路径板逐值一致；历史固定工作量平均耗时减少约 17.23%，同根名义 100 ms 的节点数增加约 17.67%。开发 Botris 2/9、while1 2/9，同期基线 2/9、1/9；额外搜索量尚未转化为合格综合强度结果。**固定工作量等价，不意味着计时搜索逐步决策必然相同**。

## 13. 风险修正：物理高度、连续消行、可见续行与恢复

这组候选研究“缓冲垃圾没上涨，却已经反复受罚”是否压掉好铺垫。它们仍保留终端危险扣分、真实垃圾结算和死亡判定，不等于无条件允许高堆。

### 13.1 SettledRisk：累计风险只用实际/预测已上涨高度

开关 `TETR_CHATGPT_SETTLED_RISK`。令 `H=roof+risen`，以 `max(0,H−12)` 取代路径积分里的 `D=max(0,H+min(8,incoming)−12)`；末端 `95D²` 保持原值。每步退还 `q×35×(D²−max(0,H−12)²)`，累计路径风险相应下降。开发 2/9、3/9，未晋级。

### 13.2 ClearBurst：连续消行只收一次最高物理暴露

开关 `TETR_CHATGPT_CLEAR_BURST`；实现 [clear_burst_evaluation.h](../src/bot/chatgpt/clear_burst_evaluation.h)。连续消行期间垃圾每步暂停上涨，所以以物理高度 `roof+risen` 计算 `E=q×35×max(0,roof+risen−12)²`，累计该消行段的最高 E，而非每步相加。

消行段内新风险为 `上一累计风险 + max(旧峰值,E)−旧峰值`；前一块不消行时旧峰值视为 0。不消行步骤继续原风险计算。terminal、奖励、出生检查不变。开发 Botris 1/9、while1 2/9，同期重复基线 3/9、2/9，没有整体改善。

### 13.3 VisiblePathRisk：可见下一块验证后退还已知暴露

开关 `TETR_CHATGPT_VISIBLE_PATH_RISK`。只在未死、没有任何未知上涨、还有可见未来且基础出生检查通过时，把新增路径风险记入可退还累计信用；在末端值加回信用。遇未知上涨撤销信用，仍保留末端压力扣分。它要求证明可见出生，未要求危险已经降到零。开发 3/9、4/9，未通过双对手门槛。

### 13.4 RecoveredPathRisk：真正回到低压才退还

开关 `TETR_CHATGPT_RECOVERED_PATH_RISK`。先累计未跨未知上涨的已知暴露；有可见续行且 `roof+min(8,incoming)≤12` 时，才把已知暴露计为恢复退款。仍有未知上涨或死亡就撤销信用。它比上一方案更严格，但开发 0/9、2/9，没有支持默认启用。

相关实现：[settled_risk_evaluation.h](../src/bot/chatgpt/settled_risk_evaluation.h)、[visible_path_risk_evaluation.h](../src/bot/chatgpt/visible_path_risk_evaluation.h)、[recovered_path_risk_evaluation.h](../src/bot/chatgpt/recovered_path_risk_evaluation.h)。这些修正能改变某个同根反例的排序；整局影响还包含有限搜索和未来信息，不能由一个案例反推出普遍强度。

## 14. B2B 储备估值与储备加连消风险

开关 `TETR_CHATGPT_CHARGE_RESERVE / TETR_CHATGPT_CHARGE_BURST`；两者均在 [charge_reserve_evaluation.h](../src/bot/chatgpt/charge_reserve_evaluation.h) 中定义。

**单独储备版**在原版分数上，未死且 B2B 至少 5 时加：

```text
ΔV = (attackWeight − b2bWeight) × (B2B−1)
   = 80 × (B2B−1)
```

意义是将已经可以释放的每个储备单位补到与未折扣真实攻击相同的单价，同时保留原活链 300 溢价。未到释放门槛不给此信用；实际释放仍由普通消行和真实规则计算。这不是每步累计奖励，也不是要求永久不断链。

**组合版**明确使用第 13.2 节的 ClearBurst 作基底，再加同一储备差价。不是单独储备版的成绩与风险版的成绩拼在一起，而是一个另行冻结、实测的候选。

开发单独储备版 Botris 3/9、while1 5/9；组合版 0/9、5/9。同期重复原版 3/9、2/9。单独版是保留的重点研究候选，但没有双对手达标；组合版说明 while1 受益不能自动推广到 Botris，也不能随意堆叠信用。

## 15. 普通 T 兑现选择 ReleaseChoice（保留 v1/v2/v3）

开关 `TETR_CHATGPT_RELEASE_CHOICE`；当前实现为 v3，来源 [release_choice_search.h](../src/bot/chatgpt/release_choice_search.h)、[release_choice_engine.h](../src/bot/chatgpt/release_choice_engine.h)、[variant_tree_node.h](../src/bot/chatgpt/variant_tree_node.h)。

默认在相同 T 几何位置上用 Spin 版本覆盖普通版本，但 Spin 续链、普通一至三消断链可以产生不同攻击与储备释放。候选继续保留原最强 Spin，同时仅在确实发生普通/Spin 碰撞、且该 T 落点消一至三行时，额外保留普通版本。

它没有新增 Spin 或断链加分，而是让原评分能比较两个真实结果。最终出招仍须按目标 Spin 标记通过宿主验证；普通版本需要真的存在普通末步路径，不能把同一 Spin 假称普通消行。

v1 的额外枚举开销较大；v2 将重复检测限到必要位置，但树复用曾丢掉 Spin 变体。v3 的 BOT 专用树缓存按变体保留身份，避免棋盘/几何相同就误合并普通、Mini、full 与 kick，未改共享对手引擎。

历史 491 组下移后重新搜索中，v2 有 169 组丢 Spin，v3 为 0；新增 394 个合法普通 T 选项通过宿主验证。开销中位数从 v1 约 4.8% 降至 v2/v3 约 1%。开发分别为：v1 1/9、4/9；v2 1/9、3/9；v3 3/9、5/9（Botris、while1）。旧程序和负面记录均保留；当前开关构建 v3，不能复现 v1/v2 冻结哈希。

## 16. 袋尾续行 BagTail 与确定单块 KnownBag

### 16.1 BagTail：枚举公开余袋可能性，给已找到的计划重排

开关 `TETR_CHATGPT_BAG_TAIL`；实现 [bag_tail_engine.h](../src/bot/chatgpt/bag_tail_engine.h)。它保留正常原生树，在预算大于 10 ms 时从总预算中留约 7 ms 给袋尾评分。

启用条件较严格：本根公开当前+NEXT 共 6 块、不是空 HOLD 根、候选未死未发生未知上涨、路径可以核对确实只消耗了这 6 个可见块。它从已有六块末端选最多 16 条路线，每个首步最多 2 条，最多检查 256 个堆中条目。不是另外启动无界尾部搜索。

对公开余袋中的每个可能下一块分别枚举合法落点，并允许使用已经已知的 HOLD；集合为空时考虑七种块。每种实际抽到的块都先检查活动块出生，再在这一抽块下选最佳续行，最后在各种抽块之间求平均。也平均核查再下一块可能的出生失败，不能假定活动块被 HOLD 换掉就不必先出生。

只完整评价的候选才参与比较；预算打断保留不完整标记，不把只算过有利抽块的部分平均当完整值。真实返回的 attack/B2B 字段仍描述六个已知放置，只有排序 value 是尾部期望；假设攻击不会计入现场。

等待和下一包垃圾沿用可见预测，不预测新对手攻击。这是一个有限候选池上的期望启发式，不是完整袋内 expectimax。开发对两个对手分别 1/12、3/12，同期基线 1/12、3/12，没有改善。

### 16.2 KnownBag：仅追加能严格确定的一块

开关 `TETR_CHATGPT_KNOWN_BAG`；实现 [known_bag_preview.h](../src/bot/chatgpt/known_bag_preview.h)。标准袋的公开补集只有一种时，追加这一种到搜索预览；补集有两种以上没有固定次序，不追加；补集为空说明进入新袋，也不追加。

它只做一次集合推断，不读取隐藏队列。HOLD 不能当成未出袋块再次计数；同一个已追加 I 也不能被 KEY_QUAD 再预留一次。当前 KEY_PIECE 和 KEY_QUAD 编译入口同时包含这项确定单块逻辑，但原版与 BagTail 不是靠同一方法扩成虚构预览。

## 17. 总材料储备与己方材料储备

开关 `TETR_CHATGPT_MATERIAL_RESERVE / TETR_CHATGPT_OWN_MATERIAL`。两版保持原规则、几何、地形、攻击和风险，仅在活末端增加有界材料价值。

**总材料版**：`ΔV=50×clamp(occupied,0,60)`，至多 3000。希望避免少量普通消行过早耗掉可用于未来 Spin/四消的工作堆；超过六行实格数量不再增长，减少无界堆高诱因。它没有证明这些实格确实形成可兑现结构。

**己方材料版**：`ΔV=50×clamp(occupied−9×garbageHeight,0,60)`。标准单洞垃圾每行自带 9 格，不由自己落块制造；扣除这些格，避免挖掉垃圾同时失去与己方材料相同的储备信用。这里仍是估值近似：垃圾行被补入的自有格可能影响计数，未知未来上涨也不在已存棋盘内。

两版开发均 Botris 2/12、while1 4/12，同期基线 1/12、3/12。己方材料版前九自然局为 2/9、2/9，总材料为 1/9、4/9，不能只截取最有利窗口。没有达到综合强度目标，默认关闭。

来源：[material_reserve_evaluation.h](../src/bot/chatgpt/material_reserve_evaluation.h)、[own_material_evaluation.h](../src/bot/chatgpt/own_material_evaluation.h)、[材料研究报告](BOT_CHATGPT_STRATEGY_20260927.md)。

## 18. 地形 V1/V2：允许可修浅洞与一条主井

开关 `TETR_CHATGPT_TERRAIN_V1 / TETR_CHATGPT_TERRAIN_V2`；仅替换末端地形，其他评分继承第 6–8 节。来源 [terrain_model.h](../src/bot/chatgpt/terrain_model.h)、[terrain_bridge_model.h](../src/bot/chatgpt/terrain_bridge_model.h)。

V1 地形公式是：

```text
T1 = −16000 −16×总列高 −72×最高列高
     −140×浅洞 −620×埋洞 −80×额外上盖格
     −48×起伏 −16×陡坡平方 −80×次井三角深度
     −45×主井超过4层的平方 +70×已就绪主井行数
```

浅洞必须同时满足：空格通过向下/水平空格路径连到顶部、同列上盖实格不超过 2、顶部到洞格距离不超过 3。其他覆盖空格按埋洞；额外上盖为第一格之外的同列上盖数。这个连通性估计不是四格方块的真实进入证明。

它在“无主井”以及每个有深度的列作为主井之间竞争，按相同权重选最好表面值。一条主井的相邻边不计普通起伏；其他井仍罚；主井深超过 4 的部分平方罚，最多奖励 4 行只缺该井列的连续就绪行。因此允许一条进攻/挖掘通道，但不能靠许多深井一起骗豁免。陡坡项只计相邻高差超过 2 的部分平方。

V2 保持全部 V1 权重，只把部分薄桥下埋洞重分类为浅洞：同列至多两格上盖，每层覆盖行只缺连续的一至两格，依次假想拆掉覆盖行后，待填缺口必须仍有垂直入口；无关上方阻挡不能被同时删除。每个合格洞少罚 `620−140=480`。

V2 没有保证 NEXT 内有适配块，也没有合法重放全部拆桥动作，属于乐观修复成本。后来的 KEY_PIECE/可达洞候选正是从几何估计推进到有路径和供给约束的预演。旧 V1/V2 冻结程序和对战记录位于 `runlogs/chatgpt_moch_20260926/`，默认未晋级；具体成绩按该目录固定十局报告读取，不能借用后来的空洞版本成绩。

## 19. 通用关键块旧 v3：保留具体特殊消行并评价消行后的场地

旧冻结程序位于 `runlogs/chatgpt_key_piece_20260927/`，SHA256：

```text
349aabe2cbfb331ea887c6c99ac9e4c08e71b05afcf994a22d3981d179d11b75
```

这一版把此前只认 I 四消的 KEY_QUAD 扩展为 Mini、full Spin、四消和全清。关键块指被具体结构需要的块，不要求攻击很高；Mini 单消有时是狭窄槽的唯一修复/续链手段。普通消行仅作为无活跃 B2B、确实修复覆盖洞的受限补充。

共有以下策略，当前版本的后续扩展另见第 20–21 节：

1. 从合法出生与实际 SRS+ 路径识别已成形关键消行，并记录位置、块型、Spin/Mini 身份。
2. 根据公开 NEXT/HOLD 在最多 4 次未来放置内找到关键块。空 HOLD 可少等待一块，使 NEXT 第五个位置也可能进入四次放置窗口；不是任意预测不可见顺序。
3. 构造等待、必要的一步补形、最后关键消行，并按真实规则重放；等待块不能破坏入口、固定目标和 Spin 身份，不能偷用保留的 HOLD。
4. 比较预演终点与当前末端价值，只保留正向收益。完整结构给较完整信用，需补一块的弱结构正向改善减半。
5. 当前真实候选若可核对确实补成原结构，给改善 10%、至多 240 的额外完成信用。
6. 保存几何与等待方案缓存，在相邻真实决策中复用，但每次以当前压力、B2B、combo、折扣和供给重放评分。

这不是把所有等待路线穷举完：中间“停车”候选按 `100×覆盖空格数+场高²` 排序，选能够保住结构的合法路线；弱结构扫描有高度和深度限制。找到了方案是存在性证据，没找到不等于不存在。

预估只替换/提高该节点 `value`，不提前写入真实攻击、reward、combo、B2B 或棋盘。下次真实根结构失效就重新识别，不永久记“这些洞已经免罚”。

旧 v3 的同批正式比较为 Botris 2/9、while1 2/9，原版 2/9、3/9，未晋级。341 根缓存开关、物理状态与路径检查是机制验证，不是强度证明。**现在打开 `TETR_CHATGPT_KEY_PIECE` 不会重建这个旧 v3**：当前头文件包含后续可达空洞和半权重改动；复验旧版本请用冻结程序/对应源码包。

## 20. 可达空洞完整版：真实路径区分洞，并在预期消盖后重计

当前共用实现：[reachable_holes.h](../src/bot/chatgpt/reachable_holes.h)、[key_piece_geometry.h](../src/bot/chatgpt/key_piece_geometry.h)、[key_piece_evaluation.h](../src/bot/chatgpt/key_piece_evaluation.h)。早期完整版 SHA256 为 `177c603d57d9b4719cb4e9af16f2c7f4d9cadc982161de32f70001efd77087a1`；历史来源见 [可达空洞报告](BOT_CHATGPT_REACHABLE_HOLES_20260928.md)。

### 20.1 什么算可填洞

待评价洞格仍是同列上方有实格的空格。先做顶部四邻接空格连通：不连通直接视为不可进入。连通也不直接打折，继续试七种方块覆盖该格的锁定姿态，从实际出生/Clutch 按完整移动与 SRS+ 顺序证明路径。

计入的是能真正进入该格的**不同块型种数**，不是形状数，也不要求最终锁定带 Spin 攻击标签；允许旋入后平移。仅有无碰撞最终形状、但无法走进去的洞，与封洞同罚。例如：

```text
###.######
###.######
###.######
###.@#####
```

`@` 点连通，但侧袋入口不允许合法进块；同列上盖三格，其原始新洞罚为 `400+80×3=640`。

### 20.2 原始新洞罚

```text
不可进入洞：400 + 80×C
可进入洞： 240 / (1 + 0.25×K + Σ_t 1/(1+d_t))
```

`C` 是该格同列上方所有实格数，跨间隙继续数，不是绝对高度或距离；每个洞分别计上盖。`K` 是能合法进入的块型种数。求和只对这些块型中公开供给可用者，`d_t` 是使用该块前要先锁定的块数：NEXT 首块/可用 HOLD 为 0，空 HOLD 可少等一块。预览耗尽时不把 HOLD 当无条件可用，不可见种类没有到时信用。

逐格罚分不证明多个洞能同时按一条路线修复，NEXT 距离也不保证所有等待块都会保住通道。正因此，关键块收益还要另行重放合法等待和消行。

### 20.3 洞罚与关键块预演共同作用

完整新洞版退回旧地形中的含洞行、连续洞深、覆盖清除成本，再扣新洞罚。高度、变化数与井项不变。关键块预演终点用同一新洞罚：上盖全清后该格不再是覆盖洞；部分清除后按剩余上盖与新入口重分类。

不能既使用消行后地形又重复加一份“原洞免罚”。预演未兑现只是一笔当前末端信用；真实状态继续按本次落块结算。

较新节点还会在当前棋盘识别七种块的已就绪 Spin/Mini/四消/全清，不只依赖根的预留表。根弱结构扫描通常只在场高不超过 16 时启用，浅层弱结构评分限制在深度不超过 2；真正消行后新暴露的弱四消有三行井检测。深层全部弱 Spin 补形没有穷举；尝试完整扫描的成本过高。

### 20.4 几何缓存的复用条件

整板洞分类缓存含完整 40 行与前次消行状态；同盘的真实可进入块型可以复用，但 NEXT/HOLD 到时信用每次重算。路径成功证明校验必要空格和较早 kick 失败需要的实格；失败连通分量仅在精确棋盘/出生条件相符时复用。上下文重建会使旧节点指针失效，因此会重初始化缓存。

未知上涨的底部仅作保守实心基底检查已知地形，不编造方便进入的垃圾洞；关键块正向预演遇未知上涨或死亡停止信用。

完整版旧批次对 Botris 20 次尝试为 0/20，原版 5/20，0 截断。独立几何对拍和缓存分数一致说明计算按设计实现，不说明花这些时间比展开更多路线更值。

## 21. 当前 KEY_PIECE：计算优化与两项半权重

当前候选开关仍为 `TETR_CHATGPT_KEY_PIECE`，默认关闭；现有独立程序 `build-reachable-holes/tetr` 的 SHA256：

```text
6fc05b05ddac50ff240e78aa775d75a899e8e02ebe646b5030cff5c75e49ce8d
```

它继承第 19–20 节的识别、供给、预演和几何规则，并包含以下全部后续策略与优化：

- 初始化时预计算能覆盖某格的方块姿态，保持原迭代顺序。
- 按精确棋盘、出生点缓存出生区旋转/横移证明，按需构建目标列与方向的下落区间；不会每盘先无条件算全部列。
- 普通落点证明也使用完整反向移动图，同时考虑最后平移和普通旋转；严格核对首个成功 kick 和 Clutch。
- 在关键块预演中逐步检查物理状态、出生、攻击、垃圾，但只在终点算洞罚；中间洞罚没有用于下步物理结算，所以省略重复计算保持原终点结果。
- 新洞罚替换比例为 0.5，关键块预估信用比例为 0.5；两项分别冻结，不更改基础攻击/B2B/挖垃圾/风险权重。

令 `L` 为旧三项洞罚，`N` 为新洞罚，`V0` 为原版分数：

```text
S = V0 + 0.5×(L−N)
```

也就是旧、新洞罚等权混合。令合法完整预演终点值为 `P`，预演终点本身也使用混合洞罚：

```text
完整机会： V = S + 0.5×max(0, P−S + completionBonus)
弱机会：   V = S + 0.25×max(0, P−S)
```

完成奖励原始为改善的 10%、至多 240，随后也乘 0.5，实际至多 120。弱机会先乘原来的 0.5，再乘总预估比例 0.5，最终为四分之一。系数在昂贵计算之后乘，不会因为半权重就自动少做一半几何查询。

正式新种子 73702001–73702020、Botris 额度 1:1.3 的结果：

| 己方指标 | 原版 | 优化半权重版 |
|---|---:|---:|
| 胜/负/截断 | 2/18/0 | 3/17/0 |
| 每局平均块数 | 245.85 | 160.05 |
| 攻击/块 | 1.116 | 0.988 |
| 抵消后送出/块 | 0.599 | 0.533 |
| 每 100 块挖垃圾 | 44.80 | 39.96 |
| 平均搜索 ms | 100.10 | 102.32 |
| 回退次数 | 0 | 0 |

虽然多一场胜利，火力、平均存活和挖掘下降；不能用单项胜差证明总体较强。此前 5/20 对 0/20 是另一批种子，与这里分开。

85 个固定局面×5 控制的纯计算优化，答案、分数和评估节点一致；固定工作量平均耗时 590.49→258.99 ms，按同时原版控制归一约两倍提速。但同根 100 ms 的最终半权重版仍只有约 35043 个平均节点，对应原版约 154279，恢复有限。

19 个中盘垃圾根进一步消融：同根同预览热缓存 100 ms，原评分平均 160101 节点，单独半权重洞罚 17021，单独预演 73783，两项都启用 12726。固定 64 轮完整版约 225.93 ms，而原评分约 14.13 ms。洞分类在垃圾复杂局面消耗大量预算，根缓存命中无法抵消分支棋盘持续改变的成本。

真实回放中，四消攻击下降解释攻击差约 61.1%，四消挖掘下降解释挖掘差约 67.6%；连消加成也下降，但这两种拆分有重叠不能相加。保持完整新评分、给更多固定轮数，部分根能找到分数更高的四消衔接，支持“没来得及展开”的解释；另有关键块等待推迟挖掘的例子，尚未量化其独立整局贡献。

这份候选完整保留，当前最值得继续处理的是逐点评价成本与有界候选复评，而非只降低奖励系数。此处是已知机制与后续研究方向，没有把尚未实现的筛选器当成交付策略。

依据：[完整优化报告](../runlogs/chatgpt_holes_optimized_20260928/REPORT.md)、[火力/挖掘下降机制](../runlogs/chatgpt_efficiency_mechanisms_20260928/REPORT.md)。

## 22. 历史保 B2B 挖掘 v5：归档保留，当前不重新启用

该机制曾叠加在全权重可达空洞与关键块评分之上。此前已按用户要求移除了构建选项、评分器、额外搜索和专用测试；**本次回退没有重新引入它**。历史完整源码位于 `runlogs/chatgpt_behavior_20260928/b2b_dig_v5/validated_sources.tar.gz`，冻结程序 SHA256 为 `14839fdcbf3f60cbf6135836502903136eaef8b721baeb68092c5746f3208073`。

共有以下策略：

1. 只在根有活跃 B2B 和待挖原垃圾时启用额外偏好。高度 9–13 平滑开启，17–21 随紧急程度关闭；来袭负载为就绪行加一半未就绪行，达到 3 时关闭；底部至多 12 行垃圾洞位变化比例达到 0.75 时关闭。
2. 合格路线必须真实/合法预演挖掉原垃圾行，不因停车保链就加奖。基本挖掘信用每行 420、至多 4 行，速度项为 `clamp(挖出行数−0.5×(放置数−1),0,1)`，另有受限 B2B 机会信用。
3. 死亡、未知上涨、无进展或比根高出超过 2 行均不能维持该信用。根压力决定权重并在分支中冻结，不允许叶节点自己切换风格赚分。
4. 合格高场低来袭局面约 20% 搜索时间用于独立保链通道，保留不立即挖掘的铺垫；主搜索仍有普通断链出口。
5. 通道提交的候选必须在至少 `min(4,公开块数)` 个可见放置中平均每两块挖至少一行，并用完整关键块/洞罚重评分后超过正常结果才采用。未知活动块之后的已知 HOLD 尾部不计入速度证明，使用前核查七种活动块能出生。
6. 巨大连击允许断链：连续消行链至少 4（含根既有历史），可见后续实际攻击至少 20，且扣除已有 B2B 充能释放部分；达到条件可取得同上限的机会信用。单纯释放长链不冒充巨大连击。

它通过了精选片段、固定工作量、连续重新决策和几何检查，仍在最终对 Botris/while1 各九局中均 0 胜。保留其 108 局开发结果与所有版本快照，是为了研究和复查，不作为默认强度依据。详细历史公式和案例见 [行为报告](BOT_CHATGPT_BEHAVIOR_20260928.md)。

## 23. 全部保留构建选项的策略索引

以下 38 项在稳定预设中全部 OFF。本文第 10–22 节是代表版本的完整继承与改动说明；下表为其余实验提供机制和源码入口，避免把仍存在的代码误认为默认策略。成绩属于各次冻结产物；表中“当前实现”不自动获得历史旧版成绩。

| 选项（前缀均为 `TETR_CHATGPT_`） | 改动的具体策略 | 当前源码 |
|---|---|---|
| `P2_A` | 同一时间预算约 80% 正常搜索、20% 检查等待节点，给真实可达 Spin 路线续算机会，最终按旧分数选 | [spin_quota_engine.h](../src/bot/chatgpt/spin_quota_engine.h) |
| `P2_B` | 同一配额内局部每层保留至多 4 条路线，可有两步 Spin 铺垫；没有已验证 Spin 时给其留一个槽 | [spin_beam_quota_engine.h](../src/bot/chatgpt/spin_beam_quota_engine.h) |
| `P3_A` | 用分支内空 HOLD 标记修正即时评分用 NEXT 指针/长度，不改原树之后的实际推进 | [hold_context_evaluation.h](../src/bot/chatgpt/hold_context_evaluation.h) |
| `P3_B` | 未知上涨后抬高已知格，在宽松几何中筛掉确定不可进入的续行；不假造新垃圾洞，也不宣称证明整路 | [rise_context_evaluation.h](../src/bot/chatgpt/rise_context_evaluation.h) |
| `P5_A` | 各层完整入口移动覆盖，第 10.1 节 | [coverage_search.h](../src/bot/chatgpt/coverage_search.h) |
| `TERRAIN_V1` | 浅洞/埋洞、单主井、井就绪与表面评分，第 18 节 | [terrain_evaluation.h](../src/bot/chatgpt/terrain_evaluation.h) |
| `TERRAIN_V2` | V1 加薄桥重分类，第 18 节 | [terrain_bridge_evaluation.h](../src/bot/chatgpt/terrain_bridge_evaluation.h) |
| `LEARNED_TERRAIN` | 原地形叠加冻结 Moch 特征线性修正 `560×Σθ_i×(f_i−mean_i)/std_i`；13 维包括高度、洞、井、材料与桥；没有在线训练 | [learned_terrain_evaluation.h](../src/bot/chatgpt/learned_terrain_evaluation.h)、[导出模型](../src/bot/chatgpt/learned_terrain_model.h) |
| `LOCAL_PATTERN` | 原评分叠加冻结局部形状表，学习被选块型内的落点排序，未读取比赛中教师或隐蔽未来 | [local_pattern_evaluation.h](../src/bot/chatgpt/local_pattern_evaluation.h)、[模型](../src/bot/chatgpt/local_pattern_model.h) |
| `TIME_B2B` | 原末端 B2B/活链 300 项整体乘 `0.96^depth`，使储备也随等待折扣，真实攻击不变 | [time_b2b_evaluation.h](../src/bot/chatgpt/time_b2b_evaluation.h) |
| `KNOWN_BAG` | 仅单元素公开余袋补集追加一块，第 16.2 节 | [known_bag_preview.h](../src/bot/chatgpt/known_bag_preview.h) |
| `O_ROOF` | 只修 O 的高空快速下降边界，其余块保留原枚举；只改变候选集合 | [o_roof_search.h](../src/bot/chatgpt/o_roof_search.h) |
| `MOCH_MULTI_STEP` | 冻结五步拟合给原七项地形统一乘 0.5，包括空板由 −16000 变 −8000，其他奖励/风险不变 | [moch_multi_step_evaluation.h](../src/bot/chatgpt/moch_multi_step_evaluation.h) |
| `RISE_HORIZON` | 第一次未知垃圾上涨仍按原版结算，之后冻结该分支估值；不把上涨本身判死亡 | [rise_horizon_evaluation.h](../src/bot/chatgpt/rise_horizon_evaluation.h) |
| `TRANSPOSITION` | 完整状态键和奖励/风险分量支配去重，第 11 节 | [transposition_engine.h](../src/bot/chatgpt/transposition_engine.h) |
| `GARBAGE_ROLLOUT` | 可见垃圾按十个假设洞位场景实体化，保留窄单路线续算，场景不是真实未来洞位 | [garbage_rollout_engine.h](../src/bot/chatgpt/garbage_rollout_engine.h) |
| `ROOT_COVERAGE` | 仅真实根完整覆盖并修复升根缓存，第 10.2 节 | [root_coverage_engine.h](../src/bot/chatgpt/root_coverage_engine.h) |
| `GARBAGE_BEAM` | 在上一垃圾采样框架内将每场景续算宽度从 1 改为 2 | [garbage_rollout_engine.h](../src/bot/chatgpt/garbage_rollout_engine.h) |
| `SETTLED_RISK` | 路径风险仅收已在场上的高度，末端仍计缓冲，第 13.1 节 | [settled_risk_evaluation.h](../src/bot/chatgpt/settled_risk_evaluation.h) |
| `FAST_TERRAIN` | 数值等价的位运算/稀疏洞扫描，第 12 节 | [fast_terrain_evaluation.h](../src/bot/chatgpt/fast_terrain_evaluation.h) |
| `CLEAR_STALL_RISK` | 仅消行确实暂停上涨时退还本步缓冲风险差；不消行保留原费 | [clear_stall_risk_evaluation.h](../src/bot/chatgpt/clear_stall_risk_evaluation.h) |
| `CLEAR_BURST` | 连续消行段只收最高物理风险，第 13.2 节 | [clear_burst_evaluation.h](../src/bot/chatgpt/clear_burst_evaluation.h) |
| `CLEAR_RECOVERY` | 用公开供给内一/两次连续消行的合法见证评价临时盖洞修复，不能只看可连通形状 | [clear_recovery_evaluation.h](../src/bot/chatgpt/clear_recovery_evaluation.h) |
| `SETUP_RECOVERY` | 上述见证允许先准备一步，但要求已知 incoming 为空，限制乐观等待 | [clear_recovery_evaluation.h](../src/bot/chatgpt/clear_recovery_evaluation.h) |
| `CHARGE_RESERVE` | 已可释放 B2B 储备单价补差，第 14 节 | [charge_reserve_evaluation.h](../src/bot/chatgpt/charge_reserve_evaluation.h) |
| `CHARGE_BURST` | 明示组合储备补差与连续消行峰值风险，第 14 节 | [charge_reserve_evaluation.h](../src/bot/chatgpt/charge_reserve_evaluation.h) |
| `SPAWN_COLUMNS` | 按正常出生涉及的列计算危险高度，代替全场最高列用于压力估计，出生碰撞仍另查 | [spawn_columns_evaluation.h](../src/bot/chatgpt/spawn_columns_evaluation.h) |
| `FLAT_ATTACK` | 只取消真实模型攻击收益的深度折扣，挖垃圾/用块成本与风险维持原折扣 | [flat_attack_evaluation.h](../src/bot/chatgpt/flat_attack_evaluation.h) |
| `BURST_WINDOW` | 计算三次放置的净送出峰值，上限 16，只对超过一次上涨容量 8 的部分加攻击权重信用；不等于游戏一秒 spike | [burst_window_evaluation.h](../src/bot/chatgpt/burst_window_evaluation.h) |
| `VISIBLE_PATH_RISK` | 已知几何与可见下一块安全后退还路径暴露，第 13.3 节 | [visible_path_risk_evaluation.h](../src/bot/chatgpt/visible_path_risk_evaluation.h) |
| `RECOVERED_PATH_RISK` | 回到原低危险阈值才退还路径暴露，第 13.4 节 | [recovered_path_risk_evaluation.h](../src/bot/chatgpt/recovered_path_risk_evaluation.h) |
| `RELEASE_CHOICE` | 保留 T 的普通/Spin 双版本供主动兑现，第 15 节 | [release_choice_engine.h](../src/bot/chatgpt/release_choice_engine.h) |
| `BAG_TAIL` | 有限已有计划池的公开袋尾期望重排，第 16.1 节 | [bag_tail_engine.h](../src/bot/chatgpt/bag_tail_engine.h) |
| `MATERIAL_RESERVE` | 至多 60 格的总材料终端储备，第 17 节 | [material_reserve_evaluation.h](../src/bot/chatgpt/material_reserve_evaluation.h) |
| `OWN_MATERIAL` | 材料估值扣除每行已知垃圾原有 9 格，第 17 节 | [own_material_evaluation.h](../src/bot/chatgpt/own_material_evaluation.h) |
| `RELATIVE_COVER` | 覆盖清除成本用上盖相对洞深替代绝对行号，其他高度/风险不变；没有进块证明 | [relative_cover_evaluation.h](../src/bot/chatgpt/relative_cover_evaluation.h) |
| `KEY_QUAD` | 只认具体可达 I 四消，并给一次准备与至多四块等待的末端估值；等待模型比通用关键块粗，不继承新洞罚 | [key_quad_evaluation.h](../src/bot/chatgpt/key_quad_evaluation.h) |
| `KEY_PIECE` | 当前为完整关键块几何/等待、新洞罚混合、半权重预演和缓存优化，第 19–21 节 | [key_piece_evaluation.h](../src/bot/chatgpt/key_piece_evaluation.h) |

此外，`policy.h` 的旧快挖混合、阶段 6 完整下降修复补丁、阶段 7 缓存/Spin 机会补丁、阶段 8 每洞 80 罚分，以及历史 B2B_DIG 都有历史记录，不等于当前 38 个有效选项。两个旧补丁在 [experiments/README.md](../src/bot/chatgpt/experiments/README.md) 中说明；阶段 4/5/6/7 的独立候选源码和二进制也继续留在 `runlogs/chatgpt_stage*/`。

## 24. 默认版本和代表候选怎样构建、运行与恢复

新增 [CMakePresets.json](../CMakePresets.json) 把全部 38 项 OFF 作为共同基底。CMake 会记住既有目录中的 ON，因此仅运行没有显式开关的旧构建命令，可能仍沿用上一次候选；稳定预设会显式覆盖这些缓存。

本机 CMake 4.4.2 已确认预设可读取。预设格式要求 CMake 3.21 及以上；项目原先的手动 CMake 3.16 构建方式仍可用。

默认回退/重建：

```sh
cmake --preset chatgpt-stable
cmake --build --preset chatgpt-stable --parallel 4
./build/tetr
```

游戏菜单选择 `Settings -> AUTOPLAY -> Bot -> bot_chatgpt`，配置索引仍为 9。这次更换的是该 BOT 的策略默认构建，不强行改写用户已保存的选 BOT/键位设置。

代表候选各自使用新构建目录，示例：

```sh
cmake --preset chatgpt-key-piece
cmake --build --preset chatgpt-key-piece --parallel 4
./build-chatgpt-key-piece/tetr
```

| 预设 | 候选 | 独立程序位置 |
|---|---|---|
| `chatgpt-key-piece` | 当前优化半权重可达空洞＋关键块 | `build-chatgpt-key-piece/tetr` |
| `chatgpt-coverage` | P5-A 完整入口覆盖 | `build-chatgpt-coverage/tetr` |
| `chatgpt-transposition` | 状态去重 | `build-chatgpt-transposition/tetr` |
| `chatgpt-fast-terrain` | 原地形等价计算优化 | `build-chatgpt-fast-terrain/tetr` |
| `chatgpt-charge-reserve` | B2B 储备估值 | `build-chatgpt-charge-reserve/tetr` |
| `chatgpt-release-choice` | 普通 T 兑现 v3 | `build-chatgpt-release-choice/tetr` |
| `chatgpt-bag-tail` | 公开袋尾期望续行 | `build-chatgpt-bag-tail/tetr` |
| `chatgpt-material-reserve` | 总材料储备 | `build-chatgpt-material-reserve/tetr` |
| `chatgpt-own-material` | 己方材料储备 | `build-chatgpt-own-material/tetr` |

每个候选继承全 OFF 配置后只开一个选项，构建目录不同于 `build`、已有 `build-reachable-holes` 和全部旧 runlogs，避免覆盖既有冻结程序。预设提供重建入口，本次未额外重建所有候选或重新宣称其成绩。

Windows 多配置生成器的程序可能在对应构建目录 `Release/tetr.exe`，构建预设已指定 Release。本文运行检查仅在 Linux 完成。

低版本 CMake 可使用一个**从未使用过的新目录**，所有源码 `option()` 的默认值都是 OFF：

```sh
cmake -S . -B build-chatgpt-stable-fresh -DCMAKE_BUILD_TYPE=Release
cmake --build build-chatgpt-stable-fresh --target tetr --parallel 4
```

若重复使用该目录且曾开过实验，需逐项显式关闭。完整 OFF 名单另存为 [stable_cache_variables.json](../runlogs/chatgpt_default_restore_20260928/stable_cache_variables.json)，不能仅关闭 KEY_PIECE 就假定别的候选也已关闭。

针对当前源码新开的候选，计时搜索和编译环境可能改变二进制或行为；旧强度对照请优先使用冻结程序及其 `metadata.json / source.tar.gz`，新程序另记哈希。回放的分析复算、离线固定轮数和实际对战要分别报告。

## 25. 本次保留的程序、源码与核验记录

此次操作前创建 [回退归档目录](../runlogs/chatgpt_default_restore_20260928/)，新增以下材料：

- `default_before.tetr`：操作前原版副本；`default_before.CMakeCache.txt`：当时构建缓存。
- `reachable_holes_before.tetr`：操作前现有半权重空洞候选副本，SHA256 仍为 `6fc05b05…49ce8d`。
- `source_before.tar.gz`：当前 bot_chatgpt 策略、候选、分析代码、BOT 头文件、宿主桥声明与 CMake 文件快照，范围见归档目录；不是整个仓库的备份。
- `preserved_candidates_before.json`：586 个已存在的历史程序、源码包、metadata、补丁以及当前空洞程序/缓存的路径、大小、SHA256；最终逐项重新核查无删除、无改写。
- `stable_cache_variables.json`：38 个实验的全 OFF 清单。
- `validation.json`：默认程序哈希、源码/预设一致性、构建与本次定向回归/冒烟结论。
- `zen_smoke/`：真实主程序 60 块单人运行的逐块日志、诊断、元数据和原生回放。

所有旧候选仍在原目录：阶段 5 F 与 v0.3.1 对照、各阶段开发/独立组、P2/P3/P5、Moch 拟合、恢复/兑现/材料、KEY_QUAD、旧关键块 v3、完整版空洞、历史保链 v5、优化半权重空洞。不能只保存一个二进制却删除该版源码/失败记录；本次 586 项哈希清单覆盖这些已有归档并在结束时复核。

本次已完成：稳定预设重新配置；`tetr` 与所需测试目标构建；9/9 定向 CTest（基础 BOT、原生接口、Spin、关键块、可达空洞、确定袋尾、HOLD 上下文、完整覆盖、去重）通过；真实 `build/tetr` 在 seed 73802001、宽度 300 下完成 60 块 ZEN、攻击 52。短运行只作集成核验，不增加强度结论，不代替 1000 块速度验收或新对战。

默认程序哈希与现有已冻结原版完全相同，候选保留状况另见 `validation.json`。本次没有修改策略权重、对手算法或游戏规则，没有重跑全套网络测试、Windows或新的强度矩阵，也没有提交/推送。

## 26. 后续研究时必须保留的解释规则

1. 默认 F 是目前保留的综合基线。储备估值与普通 T 兑现等候选有小样本正面观察，仍需同一冻结策略的独立综合比较；不得将不同候选各自最好对手的成绩拼成达标版本。
2. 完整移动、正确物理结算、缓存精确性是机制正确性；胜率、送出攻击、挖掘和生存是行为结果，彼此不能互相代替。
3. 地形/预演调整的数值不能与原版裸分直接比较大小来推断强度。应同根、同供给核查路线，再用真实主程序比较。
4. 固定深度不等于固定搜索量；新评分每节点昂贵时，深度相近仍可能少一个数量级的中层分支。
5. 缓存只可复用满足依赖条件的几何/方案。压力、折扣、NEXT/HOLD距离和链状态必须重算；同棋盘不等于同分数。
6. 未来对手攻击、垃圾洞位与不可见块顺序都未提供。一个能复算的计划前缀不保证之后必然执行，真实后续根必须重规划。
7. 保留协议中的所有失败、截断、中止、旧版本和负面例子；开发样本与独立确认分开，未运行的组不写成通过。

本文是本次默认回退与策略交接的统一说明；进一步的逐段棋盘、计算控制和完整比分在上述链接的原报告/JSON 中。
