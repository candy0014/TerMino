# 预设、叠加恢复与重力进程

内置预设是 TerMino 现有规则上的组合，允许进一步微调。经典块提供 NES 风格规则和手感；延迟块参考《俄罗斯方块效应》常规默认手感，只修改手感；马拉松提供150行目标及消行升级重力，保留现有规则和手感。这些组合不复制原作的完整评分、顶出判定、逐帧 DAS 状态机或段位系统。

## 使用与恢复

预设库以「#编号: 名称」列出各预设，打开后提供「使用预设 / 编辑预设 / 删除预设」。使用、编辑、删除预设、撤回最近一层、撤回全部预设、移除某层以及保留当前参数并结束预设，都需再次确认：先按 Enter / 右键显示确认提示，再按一次 Enter 才执行。长按不会确认，方向键取消确认，Tab 切换修改对象也会取消；进入子菜单不需要再次确认。

「编辑预设」载入已保存的参数，未定义项仍为未定义；保存覆盖相同编号的预设，放弃修改保留最近一次保存的版本，不改变玩家当前配置或已经应用的层。双人预设分别载入两名玩家的参数。三个内置预设也可以编辑和删除。「删除预设」只移除预设库中的条目，其他条目的编号保持不变；新建预设继续使用新的编号。已应用的层保存独立参数快照，编辑或删除库中的原预设后仍能正常撤回，后续使用则取库中的最新保存值。

根菜单「预设」一次应用增加一层。同一预设也可重复叠加。一般由新的层覆盖重合参数；马拉松的目标和重力曲线始终优先于经典块的单独默认值。因此经典块→马拉松、马拉松→经典块的生效配置相同，均为经典规则与手感、150行目标和马拉松重力曲线。手动修改归到实际生效的来源层，未被任何层涉及的参数保持普通个人配置。普通参数的提示显示来源与「已调整」。三种预设均保留撤回、暂停、BOT选择/速度、模拟时间和轮流设置。经典块保留原有的竞速目标；在与经典重力冲突的非竞速单人模式下改为自由练习，叠加马拉松时使用马拉松目标。

「撤回最近一层」恢复上一层，包括它先前的微调；「撤回全部预设」恢复所有仍被各层涉及的基础值。「已应用的预设」按旧到新列出，可移除中间层并重新计算其他层。已经不再被任何层涉及的参数恢复基础值后退出恢复记录，此后对它的修改作为个人配置保留。「保留当前参数并结束预设」需再次按 Enter，保留数值并清除恢复记录。

预设自身的「模式 = 单窗口对战」时，使用预设会同时为玩家 1、玩家 2 叠加各自对应的参数，即使当前只选中其中一名玩家；当前为单人时也会进入单窗口对战。预设模式为其他值或未定义时，只作用于当前修改对象；当前明确选择「双方」则分别应用于双方。曾编辑过双人参数但后来改成单人模式的预设，不会因此自动影响另一名玩家。此规则同时适用于普通设置和预设编辑器中的叠加。

每位玩家有独立基础值与层记录；双方操作先在两个副本上计算，成功后一起更新。层数、顶层和待撤回名称不同会并列显示。对战期间保持对战模式，150 行完成目标只用于单人。对战中的垃圾接收与发送仍由比赛规则决定。

基础值、每层给定值、微调值、顺序和版本一起写进当前系统玩家配置的 `[PRESETS]` 节，和生效配置通过同一次临时文件替换保存。关闭设置、重新开局和重启程序都可恢复；已保存的层保留当时给定值，内置预设调整后可撤回旧层再应用新版；回放只保存最终生效的运行规则，不保存恢复记录。无法解析的层记录不会改变配置文件里的生效参数。普通类别重置按普通编辑记录，保留预设恢复记录；根菜单或玩家菜单的全体重置会清空所选玩家的全部预设层、原值和微调记录，选择双方时同时清空双方记录。

## 新建预设

「预设 → 新建预设」打开独立的预设设置界面，复用普通设置的输入、选择、按键绑定、预设叠加和地图编辑功能；其中不再提供「新建预设」。所有参数初始显示「未定义」，可以逐项赋值，选中参数按 U 恢复未定义。数值输入也接受 `未定义`、`undefined` 或 `?`。未定义的参数不写进预设，应用时保留玩家当前值。

选择「模式 = 单窗口对战」后，修改对象提供玩家 1、玩家 2、双方，Tab 可在当前条目切换。两名玩家分别保留自己的草稿和未定义状态；S 保存整个预设，退出检查也覆盖两份草稿。重置为原设置时使用对应玩家的快照，双方重置分别处理两名玩家。切换回单人模式后，已编辑的另一名玩家参数仍保留；保存使用当前活动的游戏模式。控制变量未定义时允许配置相关参数，赋值后按普通设置的条件显示分支。

根菜单提供「重命名预设」和「保存预设」，S 可直接保存并继续编辑。文字输入时 S / U 作为输入字符，确认后再使用快捷键。Esc 在参数输入中取消；从菜单退出时，有修改则选择「保存退出 / 不保存退出 / 不退出」，没有修改直接返回原设置菜单。已保存的版本始终保留。地图仍单独保存在地图库，退出预设时丢弃修改不会撤销已保存的地图。

预设界面内的「重置」提供「全部恢复到未定义 / 使用原玩家的设置 / 使用默认设置 / 取消重置」，只处理当前条目包含的变量，包括该条目的子菜单变量；原玩家设置取进入编辑器时的快照。根菜单重置覆盖所有设置变量；当前为单窗口对战时保留该模式及其已定义状态。普通设置根菜单的重置同样保留单窗口对战模式，选择玩家 1、玩家 2 或双方时均不会退出对战。

经典块、延迟块、马拉松与自建预设使用统一的字段格式和应用流程，库文件为 `.tetr/presets.txt`；首次保存时把三个内置预设一起写入。库格式版本 2 支持分别保存玩家 1 / 2 的参数，并兼容版本 1 的统一参数预设；应用双人预设时按玩家取对应参数，未定义项保留该玩家的设置。内置预设保留现有叠加规则，后加的自建预设可以覆盖其中已定义的参数。玩家配置的恢复记录保持版本 2，仍可读取旧版本 1。删除地图并重编号时，也会更新两名玩家的预设地图引用。

## 独立使用递增重力

「游戏规则 → 重力 → 重力模式」提供固定周期、关闭、20G、NES 递增、马拉松递增。递增模式无需预设即可选择，并显示「起始间隔（秒）」「每级消行数」「加速倍率」。

令每级消行数为 N，加速倍率为 a，起始下落间隔为 t。NES 的速度级为 `floor(消行数/N)`，最高29；马拉松为 `1+floor(消行数/N)`，最高15。实际间隔为 `t × (当前级参考间隔 / 第一级参考间隔)^a`。默认 N=10、a=1；a 大于1增速更强，小于1更缓。修改起始间隔会等比例缩放整条速度曲线。

NES 曲线使用 NTSC 的48、43、38……1帧/格表；经典预设初始间隔48/60.0988秒。马拉松曲线使用 TGM4 Standard Marathon 的15级内部重力表，按初始重力1092与当前级内部重力的比值缩放；预设起始间隔1秒。新重力参数写入回放；旧回放默认为固定周期。QuickPlay 和非固定速度的限时挑战继续使用自己的速度规则，菜单会提示这些参数在当前规则下不生效。

## 资料与参数选择

- [Hard Drop：NES Nintendo](https://harddrop.com/wiki/Tetris_(NES,_Nintendo))：10×20、单预览、NRS、无 Hold / 硬降 / 锁定延迟，NTSC DAS16帧、ARR6帧，软降2帧/格及重力表。原作进块10～18帧、消行17～20帧随条件变化；本程序的固定延迟预设取较短端10与17帧。DAS 预充关闭以避免现代进块预充；原作冻结、墙充等状态行为不由预设复制。
- [《俄罗斯方块效应》官方社区指南](https://www.tetriseffect.game/beginners-community-guide/)：常规默认 DAS 166ms（10帧）、ARR 30Hz（2帧）。[TetrisWiki：准则](https://tetris.wiki/Tetris_Guideline)交叉核对10帧DAS、2帧ARR和6帧进块延迟；[准则游戏差异表](https://tetris.wiki/Guideline_compliant_game_differences)核对效应的20倍软降、15次锁定重置。[效应模式表](https://tetris.wiki/Tetris_Effect)记录低等级锁定约0.5秒、消行500ms。延迟块取锁定/消行30帧、进块6帧，保留既有重力；DCD=0、预移动/预充开启、有限锁定重置。以常规手感为参考，不复制原作特殊模式中的动态延迟、Zone或逐帧输入细节。
- [俄罗斯方块中文维基：TGM4 绝对之眼](https://tetriswiki.cn/p/俄罗斯方块特级大师4：绝对之眼)、[TetrisWiki：TGM4](https://tetris.wiki/Tgm4)：马拉松曲线沿用 Standard Marathon 的15级内部重力表；只覆盖150行目标、重力、升级常量和与其冲突的游戏/垃圾模式，不固定 SRS、7-Bag、Hold、Next、DAS/ARR、软降、消行或进块手感。现代规则与经典规则都可以叠加这一目标。

上述来源于2026-10-09核对。每级消行数、加速倍率是本程序可调扩展；原作评分与现代攻击奖励不等同。本程序保留自己的计分体系。经典块关闭现代旋转奖励、B2B、全消/半消扩展，已经关闭的奖励金额及垃圾细节保持原值；延迟块和马拉松不覆盖这些奖励选择。

## BOT 的软降演示与 PPS

软降间隔非零时，BOT 在任意 PPS（包括最大值和0无上限）下都演示逐格下降。倍率 SDF 使用当时有效重力；固定秒数 SDF 与自然重力取更快速度。脚本中的软降、降到底以及最后的落地段都遵守该速度；20G或SDF=0继续即时下降。软降被禁用时按原有可用操作规划。

BOT 在出块后立即计算和演示，落块时等待当前PPS周期剩余时间；前一块的消行、进块和当前计算、动画均已占用周期，不再额外叠加完整等待。Hold延续同一个落块周期。小的调度超时沿既定时间线补偿，工作超出一个周期则重新对齐，避免累积大量待追赶落块。强制延迟或软降演示超过周期时，实际PPS由这些耗时限制；不会压缩它们来强行达到目标。暂停、撤回与轮流等待沿用现有时钟/状态恢复。

## 全变量检查

下表由当前 schema 与预设定义生成，列出全部配置变量。时间值为秒，除经典规则使用60.0988Hz外，`f`取60Hz。SDF 在 `SDF Fixed=1` 时表示秒/格，否则是重力倍数。重力进程0/1/2分别为固定/NES/马拉松。

「保留」表示该预设不接管该变量。经典块只覆盖经典规则及冲突开关，延迟块只覆盖手感，马拉松只覆盖目标/重力及冲突模式。未激活的QuickPlay Mod、垃圾细节、奖励金额、键位、外观、网络地址均保留。配置路径、键位路径与预设恢复结构不属于运行规则，另行维护。经典块的游戏模式在原基础配置为竞速时保留；对战应用时始终保留对战模式。马拉松重力参数在组合中优先。

| 配置变量 | 经典块 | 延迟块 | 马拉松 |
| --- | --- | --- | --- |
| MODEL / Model | 0 | 保留 | 1 |
| MODEL / Racing Distance | 保留 | 保留 | 150 |
| MODEL / Blitz Time | 保留 | 保留 | 保留 |
| MODEL / Replay | 保留 | 保留 | 保留 |
| MODEL / Time Multiplier | 保留 | 保留 | 保留 |
| GARBAGE / Garbage Model | 0 | 保留 | 0 |
| GARBAGE / Cheese Model | 保留 | 保留 | 保留 |
| GARBAGE / Num of Hole | 保留 | 保留 | 保留 |
| GARBAGE / Layer Height | 保留 | 保留 | 保留 |
| GARBAGE / Time Interval | 保留 | 保留 | 保留 |
| GARBAGE / Cheese Messiness | 保留 | 保留 | 保留 |
| GARBAGE / Cheese Density | 保留 | 保留 | 保留 |
| GARBAGE / Hit Multiplier | 保留 | 保留 | 保留 |
| GARBAGE / APM for Survival | 保留 | 保留 | 保留 |
| GARBAGE / GarbageInTime | 保留 | 保留 | 保留 |
| GARBAGE / Garbage Entry Mode | 保留 | 保留 | 保留 |
| QUICK PLAY / Expert | 保留 | 保留 | 保留 |
| QUICK PLAY / Nohold | 保留 | 保留 | 保留 |
| QUICK PLAY / Messy | 保留 | 保留 | 保留 |
| QUICK PLAY / Gravity | 保留 | 保留 | 保留 |
| QUICK PLAY / Volatile | 保留 | 保留 | 保留 |
| QUICK PLAY / Double hole | 保留 | 保留 | 保留 |
| QUICK PLAY / Invisible | 保留 | 保留 | 保留 |
| QUICK PLAY / Allspin | 保留 | 保留 | 保留 |
| QUICK PLAY / Backfire | 保留 | 保留 | 保留 |
| QUICK PLAY / Snake eyes | 保留 | 保留 | 保留 |
| QUICK PLAY / Snowball | 保留 | 保留 | 保留 |
| QUICK PLAY / Fool | 保留 | 保留 | 保留 |
| QUICK PLAY / Default floor | 保留 | 保留 | 保留 |
| HANDLING / Speed | 0.7986848323 | 保留 | 1 |
| HANDLING / SDF | 0.03327853468 | 20 | 保留 |
| HANDLING / SDF Fixed | 1 | 0 | 保留 |
| HANDLING / DAS | 0.2662282774 | 0.1666666667 | 保留 |
| HANDLING / ARR | 0.09983560404 | 0.03333333333 | 保留 |
| HANDLING / DCD | 0 | 0 | 保留 |
| HANDLING / Pre-move | 0 | 1 | 保留 |
| HANDLING / DAS Precharge | 0 | 1 | 保留 |
| GAME / Width | 10 | 保留 | 保留 |
| GAME / Height | 20 | 保留 | 保留 |
| GAME / Num of Next | 1 | 保留 | 保留 |
| GAME / Open Hold | 0 | 保留 | 保留 |
| GAME / Invisible | 0 | 保留 | 保留 |
| GAME / Anti Gravity | 0 | 保留 | 保留 |
| GAME / Ghost | 0 | 保留 | 保留 |
| GAME / EPLD | 0 | 0.5 | 保留 |
| GAME / Limit of EPLD | 0 | 15 | 保留 |
| GAME / Rotation System | 9 | 保留 | 保留 |
| GAME / Spin Bonuses | 9 | 保留 | 保留 |
| GAME / Random | 6 | 保留 | 保留 |
| GAME / 20G | 0 | 保留 | 0 |
| GAME / Line Clear Delay | 0.2828675448 | 0.5 | 保留 |
| GAME / Infinite Movement | 0 | 0 | 保留 |
| GAME / Entry Delay | 0.1663926734 | 0.1 | 保留 |
| GAME / Cancellation Multiplier | 保留 | 保留 | 保留 |
| GAME / Messiness Within Attack | 保留 | 保留 | 保留 |
| GAME / Avoid Same Column RNG | 保留 | 保留 | 保留 |
| GAME / Garbage Cap | 保留 | 保留 | 保留 |
| GAME / Opener Phase | 保留 | 保留 | 保留 |
| GAME / Simulation PPS | 保留 | 保留 | 保留 |
| GAME / Turn Pieces | 保留 | 保留 | 保留 |
| GAME / Disable Left | 0 | 保留 | 保留 |
| GAME / Disable Right | 0 | 保留 | 保留 |
| GAME / Disable Clockwise | 0 | 保留 | 保留 |
| GAME / Disable Counterclockwise | 0 | 保留 | 保留 |
| GAME / Disable 180 | 1 | 保留 | 保留 |
| GAME / Disable Soft Drop | 0 | 保留 | 保留 |
| GAME / Disable Hard Drop | 1 | 保留 | 保留 |
| GAME / Disable Undo | 保留 | 保留 | 保留 |
| GAME / Disable Pause | 保留 | 保留 | 保留 |
| GAME / Infinite Hold | 0 | 保留 | 保留 |
| GAME / Hold Count | 1 | 保留 | 保留 |
| GAME / B2B Reward | 2 | 保留 | 保留 |
| GAME / B2B Reward Amount | 保留 | 保留 | 保留 |
| GAME / B2B Charging | 0 | 保留 | 保留 |
| GAME / B2B Charge Threshold | 保留 | 保留 | 保留 |
| GAME / B2B Charge Reduction | 保留 | 保留 | 保留 |
| GAME / Enable Perfect Clear | 0 | 保留 | 保留 |
| GAME / Perfect Clear Attack | 保留 | 保留 | 保留 |
| GAME / Perfect Clear Charge | 0 | 保留 | 保留 |
| GAME / Enable Half Clear | 0 | 保留 | 保留 |
| GAME / Half Clear Attack | 保留 | 保留 | 保留 |
| GAME / Half Clear Charge | 保留 | 保留 | 保留 |
| GAME / Garbage Special Attack | 0 | 保留 | 保留 |
| AUTOPLAY / Autoplay | 保留 | 保留 | 保留 |
| AUTOPLAY / Handling Simulation | 保留 | 保留 | 保留 |
| AUTOPLAY / PPS of Autoplay | 保留 | 保留 | 保留 |
| AUTOPLAY / Width of Autoplay | 保留 | 保留 | 保留 |
| CONTROLS / Move Piece Left | 保留 | 保留 | 保留 |
| CONTROLS / Move Piece Right | 保留 | 保留 | 保留 |
| CONTROLS / Soft Drop | 保留 | 保留 | 保留 |
| CONTROLS / Hard Drop | 保留 | 保留 | 保留 |
| CONTROLS / Rotate Clockwise | 保留 | 保留 | 保留 |
| CONTROLS / Rotate Counterclockwise | 保留 | 保留 | 保留 |
| CONTROLS / Rotate 180 | 保留 | 保留 | 保留 |
| CONTROLS / Swap Hold Piece | 保留 | 保留 | 保留 |
| CONTROLS / Retry Game | 保留 | 保留 | 保留 |
| CONTROLS / Open Config | 保留 | 保留 | 保留 |
| CONTROLS / Replay | 保留 | 保留 | 保留 |
| CONTROLS / Undo | 保留 | 保留 | 保留 |
| CONTROLS / Pause game | 保留 | 保留 | 保留 |
| NETWORK / Server IP | 保留 | 保留 | 保留 |
| OTHERS / Square Version | 保留 | 保留 | 保留 |
| OTHERS / ObscuredColors | 保留 | 保留 | 保留 |
| OTHERS / UI Language | 保留 | 保留 | 保留 |
| OTHERS / Show Spawn Position | 保留 | 保留 | 保留 |
| GAME / Gravity Progression | 1 | 保留 | 2 |
| GAME / Gravity Level Lines | 10 | 保留 | 10 |
| GAME / Gravity Acceleration | 1 | 保留 | 1 |
| MAPS / Selected | 保留 | 保留 | 保留 |

全局重置、玩家完整重置先撤回所有预设层，再应用重置规则。DAS / ARR 等通常保留的个人手感恢复到应用预设前的基础值，预设层内的手动修改随层撤回。
