# 快速游戏 Mod 与 Bot 策略（2026-10-20）

本页保留 10 月 20 日的修复记录。10 月 21 日的全部 BOT 适配、混合块型修复和高度优化见 [当前 QP BOT 说明](BOT_QUICK_PLAY_MODS.md)。

## 原因

`bot_chatgpt`、`while1` 和 `zzztoj_botris` 的快速游戏入口原先只有无 Mod 时
调用各自原有的完整搜索。`QuickPlaySearch::plain()` 排除了所有 Mod，连只影响显示的
Invisible 也会切换到另一套短视野通用搜索。

这套搜索还额外加入了井口准备、爆发、经验奖励，缺少 ChatGPT 原有的 T/I 使用代价和
HOLD 储备评分。Botris 的累计攻击、连消及偏好也被不同的通用分数代替。因此切换 Mod
同时切换了 AI 策略，并非只有 Mod 的玩法规则在起作用。

## 修复

- 用 `native_policy_compatible()` 按实际落块能力选择搜索。普通/反向 Invisible、
  Expert、Messy，以及普通 No Hold、Volatile、Double Hole、Gravity，在满足原搜索的
  场地、旋转和移动限制时继续使用原 Bot。保留真实 HOLD 可用性、预览、重力、垃圾包
  和已公布 Windup；实际结算仍由原共享规则执行。原生搜索返回的目标仍须通过共享路径验证。
  ChatGPT 原有 Spin 判定也支持扩展 Allspin，扩展模式的经验历史留在主机结算。
- 普通/反向 Allspin 的重复操作限制、Backfire、Snake Eyes、反向 Double Hole、特殊块和 Snowball
  等继续由共享规则模拟，保留自返垃圾、攻击限制、变形、成长和死亡判定。
- ChatGPT 的落块奖励提取到同一个 `placement_reward()`，两条搜索路径共用攻击、挖掘、
  消行等待及 T/I 使用代价；保留 HOLD 中 I/T 的储备分。移除另一套井口、经验和爆发奖励。
  评分延续普通快速游戏的保守攻击策略，Mod 实际产生的攻击差额仍参与估值，不改变主机攻击。
- Botris 的规则搜索恢复其累计攻击、连消、容量缩放及偏好项；while1 保留自己的地形
  回调，不再叠加通用的井口、经验和爆发策略。
- 地形评分以实际可游玩高度为底部，不把疲劳或 Allspin 形成的不可游玩底部当成堆叠。
  同时修正原搜索快照将场宽误写为 20 的问题。

Mod 改变块型、攻击资格、场地或可用操作时，合理策略仍会变化；不要求这些场景与普通
模式逐块相同。快照和模拟不读取未来袋序或垃圾洞，也不消耗玩家 RNG。

## 其他 Bot

`candy0014` 和 `coldclear` 在其支持的 Mod 下没有这条通用策略切换路径。
`mech`、`zzztoj_toj`、`zzztoj_dig`、`zzztoj_qq`、`zzztoj_c2` 原有的快速游戏
可用性限制保持不变；没有通过移除限制来伪装 Mod 支持。

## 回归

- 先在修复前执行新增用例，确认开启 Invisible 后原搜索快照断言失败。
- 三个受影响 Bot 共 31 组兼容 Mod 检查原搜索、合法路径及现成四消。
- 六个固定 ChatGPT 根状态，对普通搜索和共享规则搜索的 204 个物理落点逐项比对评分
  （只消除原地形函数的固定常数），覆盖空场、四消井、不平地形、T 和 I。
- 相同可游玩场地分别使用短场地和带永久底部的场地，验证地形分相同。
- 原有全部 Mod/组合、特殊块、场地尺寸、重力、真实结算、Windup、疲劳和连续对局
  检查保留；新增 `bot_quick_play_mod_attack`，分别用扩展 Allspin、Backfire 和
  Snake Eyes 运行三个 Bot 的 100 块真实规则轨迹，检查存活和实际攻击。
  Snake Eyes 依其规则允许单消攻击，Backfire 允许降低自返垃圾的连续较小 Spin 攻击。

实现位于共享 Bot 层，终端和 OpenGL 同时使用；不改变键盘入口、设置、存档、回放或
网络格式，不操作本地 `.tetr`。本轮原生 Windows 未实机验证，验收记录保持 pending。

## 本机验证结果

Linux 终端与 OpenGL 已通过 `./compile.sh --frontend both` 更新到 `build/tetr` 和
`build/tetr_gl`。最终 `ctest --test-dir build --output-on-failure -j 4`：
**169/169 全部通过，157.64 秒**。31 组原策略检查、204 个落点评分对照、不可游玩底部
对照及九组共 900 块 Mod 攻击轨迹全部通过。

最初的沙箱运行无法创建 UDP/HTTP socket；最终完整回归已在解除此限制后执行，包含
图形会话、房间、Bot 联网和网络恢复检查。图形测试保持隐藏窗口或无窗口运行。
日志及相对本次修改前工作区的独立补丁保存在
`build-migration-backups/bot-mod-policy-20261020/`，未发布到 GitHub。
