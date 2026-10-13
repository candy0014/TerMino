#include "ui_text.h"
#include "tetr/util/terminal_text.h"
#include "tetr/config/user_config.h"
#include <cstdlib>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <linux/input.h>
#endif

namespace App::UiText {
namespace {
Language activeLanguage = Language::Chinese;
constexpr std::pair<std::string_view, std::string_view> dictionary[] = {
    {"RESULTS", "结算数据"}, {"CLEAR STATISTICS", "消行统计"}, {"PLAYER ", "玩家 "},
    {"Save Replay", "保存回放"}, {"More above", "上方还有数据"}, {"More below", "下方还有数据"},
    {"TIME & ALTITUDE", "时间与海拔"}, {"ATTACK", "攻击"},
    {"PIECES & LINES", "落块与消行"}, {"CHAINS & HOLD", "连击与暂存"},
    {"Enlarge the window to view statistics.", "请增大窗口以查看统计。"},
    {"NORMAL;SPIN;MINI SPIN  (0 lines: Spins only)", "普通；Spin；Mini Spin（0 行仅统计 Spin）"},
    {"Selected: ", "选中："},
    {"Tab: Results / Clears / Board   Arrows: Move", "Tab：结算 / 消行统计 / 结束画面  方向键：移动"},
    {"PgUp/PgDn: Player", "PgUp/PgDn：切换玩家"},
    {"[Tab] Results / Clear statistics / Board", "[Tab] 结算数据 / 消行统计 / 结束画面"},
    {"PRESET SETTINGS", "预设设置"}, {"Undefined", "未定义"}, {"New preset", "新建预设"},
    {"Rename preset", "重命名预设"}, {"Save preset", "保存预设"},
    {"Use preset", "使用预设"}, {"Edit preset", "编辑预设"}, {"Delete preset", "删除预设"},
    {"Select, edit or delete this preset.", "选择使用、编辑或删除此预设。"},
    {"Edit the saved parameters; S saves changes to this preset.", "编辑已保存的参数；按 S 保存并覆盖此预设。"},
    {"Delete only this preset from the collection; already applied layers remain available to undo.", "仅从预设库删除此预设；已应用的层仍保留，可继续撤回。"},
    {"Using, editing, deleting presets and changing layers require confirmation.", "使用、编辑、删除预设及更改预设层均需再次确认。"},
    {"Preset editor closed.", "预设编辑器已关闭。"}, {"Preset deleted.", "预设已删除。"},
    {"Preset action failed; existing data is unchanged.", "预设操作失败，已有数据保持不变。"},
    {"Press Enter again to edit this preset. Arrow keys cancel.", "再次按 Enter 确认编辑此预设；方向键取消。"},
    {"Press Enter again to delete this preset. Arrow keys cancel.", "再次按 Enter 确认删除此预设；方向键取消。"},
    {"Create a preset with every parameter undefined.", "新建预设，所有参数初始均为未定义。"},
    {"Apply the defined parameters; undefined parameters keep their current values.", "应用已赋值的参数；未定义的参数保留玩家当前设置。"},
    {"This preset uses Local Versus: apply its Player 1 and Player 2 parameters to both players.", "此预设为单窗口对战：同时应用玩家 1 和玩家 2 的对应参数。"},
    {"This preset applies to the current edit target.", "此预设只应用于当前修改对象。"},
    {"Enter confirms the preset name. Esc cancels.", "Enter 确认预设名称，Esc 取消。"},
    {"Save the current defined parameters.", "保存当前预设中已赋值的参数。"},
    {"Only defined parameters are saved. S saves; U makes the selected parameter undefined; Esc exits.", "仅保存已赋值的参数；S 保存，U 将选中参数恢复未定义，Esc 退出。"},
    {"U restores this parameter to undefined.", "U 将此参数恢复未定义。"},
    {"[S] Save preset   [U] Undefined   Esc Exit", "[S] 保存预设  [U] 恢复未定义  Esc 退出"},
    {"Left/Backspace Back   Esc Exit", "左 / 退格返回  Esc 退出"},
    {"Preset saved.", "预设已保存。"}, {"Preset draft updated.", "预设编辑内容已更新。"},
    {"Parameter restored to undefined.", "参数已恢复未定义。"},
    {"Could not save the preset.", "预设保存失败。"},
    {"Could not save the preset. Choose another option or try again.", "预设保存失败，请选择其他选项或重试。"},
    {"Could not read the preset collection. Existing presets will not be overwritten.", "预设库读取失败，已有预设不会被覆盖。"},
    {"Reset this entry", "重置当前条目"}, {"All undefined", "全部恢复到未定义"},
    {"Use original player settings", "使用原玩家的设置"}, {"Use default settings", "使用默认设置"},
    {"Cancel reset", "取消重置"}, {"RESET complete. Preset draft updated.", "重置完成，已更新预设编辑内容。"},
    {"Choose undefined, original player settings, defaults, or cancel. Only this entry's parameters change.", "选择恢复未定义、原玩家设置、默认设置或取消；仅修改当前条目内的参数。"},
    {"Esc Cancel", "Esc 取消"},
    {"Unsaved changes", "尚未保存的修改"}, {"Save before exiting?", "退出前是否保存？"},
    {"Save and exit", "保存退出"}, {"Discard and exit", "不保存退出"}, {"Keep editing", "不退出"},
    {"SETTINGS", "配置"}, {"Player", "玩家"}, {"Player 1", "玩家 1"}, {"Player 2", "玩家 2"},
    {"Maps", "地图"}, {"New map", "新建地图"}, {"Use map", "使用地图"}, {"Edit map", "编辑地图"}, {"Delete map", "删除地图"},
    {"Map preview", "地图预览"}, {"Thumbnail", "缩略图"},
    {"Field size", "场地大小"}, {"Generator", "出块方式"}, {"Final goal", "最终目标"}, {"Custom sequence", "自定义块序"},
    {"EDITOR OPTIONS", "编辑设置"}, {"MAP EDITOR", "地图编辑"},
    {"Left/Backspace/Esc Back", "左 / 退格 / Esc 返回"}, {"Enter Confirm   Esc Cancel   Blank Keep value", "Enter 确认  Esc 取消  留空保留原值"},
    {"Save map", "保存地图"}, {"Initial floor", "初始楼层"}, {"Exit editor", "退出编辑"},
    {"Rename", "重命名"}, {"Piece library", "方块库"}, {"Enter confirms the map name. Esc cancels.", "Enter 确认地图名称，Esc 取消。"},
    {"S Save map   Space Back   Tab Field / Sequence", "S 保存地图  空格返回  Tab 版面 / 块序"},
    {"S Save map", "S 保存地图"},
    {"Map mode", "地图模式"}, {"Clear map", "清空地图"}, {"All clear field", "全消版面"},
    {"OFF uses the racing line target. Clear map clears every starting cell. All clear field ends on ALL CLEAR.", "关闭时按竞速行数结束；清空地图需清除开局所有格子；全消版面需达成 ALL CLEAR。"},
    {"Do not use a map", "不使用地图"}, {"Selected map", "已选地图"}, {"Fixed rotation system", "固定旋转系统"}, {"pieces", "块"}, {"Unavailable", "不可用"},
    {"Open a map's menu directly. Creating, using, editing and deleting maps require confirmation.", "选择地图名称直接进入子菜单；新建、使用、编辑和删除地图仍需确认。"},
    {"Open the field and sequence editor.", "进入场地与序列编辑器。"},
    {"Use the ordinary field and piece settings.", "取消地图选择，使用普通场地与出块设置。"},
    {"Select, edit or delete this map.", "选择使用、编辑或删除此地图。"},
    {"Use this map's starting field and piece generation.", "优先使用此地图的初始场地和出块方案。"},
    {"Edit the field and sequence; the Save Replay key saves the map.", "编辑场地和序列，按原保存回放键保存地图。"},
    {"Edit the field and sequence; S saves the map immediately.", "编辑场地和序列，按 S 直接保存地图。"},
    {"Delete this map from the collection; other maps with the same name remain.", "删除此地图；同名的其他地图保留。"},
    {"Could not read the map collection. Existing maps will not be overwritten.", "地图文件读取失败，已有地图不会被覆盖。"},
    {"The selected map is unavailable. Choose another map or do not use a map.", "所选地图已不可用，请选择其他地图或不使用地图。"},
    {"Press Enter again to confirm this map option. Arrow keys cancel.", "再次按 Enter 确认此地图选项；方向键取消。"},
    {"Press Enter again to delete this map. Arrow keys cancel.", "再次按 Enter 确认删除此地图；方向键取消。"},
    {"Map editor closed.", "已退出地图编辑。"}, {"Map deleted.", "地图已删除。"}, {"Map selection saved.", "地图选择已保存。"},
    {"Use each map piece's original rotation system. Other systems replace matching pieces; unmatched special pieces keep their original rotation.", "使用地图中每块原有的旋转系统；选择其他系统时替换可对应的方块，没有对应定义的特殊块保留原旋转。"},
    {"Map action failed; existing data is unchanged.", "地图操作失败，已有数据未修改。"},
    {"The map sequence contains pieces unsupported by this bot.", "地图块序中包含此机器人不支持的方块。"},
    {"Presets", "预设"}, {"Classic blocks", "经典块"}, {"Delayed blocks", "延迟块"}, {"Marathon", "马拉松"},
    {"layers", "层"}, {" (adjusted)", "（已调整）"}, {"Source: ", "来源："}, {"Personal settings", "个人配置"},
    {"Undo latest layer", "撤回最近一层"}, {"Undo all presets", "撤回全部预设"},
    {"Keep current values and end presets", "保留当前参数并结束预设"}, {"Applied presets", "已应用的预设"},
    {"Remove this layer", "移除此层"},
    {"Apply adds one layer. Newer layers override shared parameters; ordinary edits belong to their latest source layer.", "应用会叠加一层；重合参数由上层覆盖，普通修改归属最近覆盖它的层。"},
    {"Restore the previous layer, including its earlier adjustments. Uncovered settings stay personal.", "恢复上一层及其已有微调；未被预设涉及的个人配置保留。"},
    {"Restore every preset-owned parameter to its original value; uncovered edits remain.", "恢复所有预设涉及参数的原值；未涉及的普通修改保留。"},
    {"Keep the current configuration and discard preset recovery. Confirm twice.", "保留当前参数，清除各层和恢复记录；需再次确认。"},
    {"Layers are listed oldest to newest. Open one to remove that layer while keeping the others.", "按应用先后列出各层；可进入任一层将其移除，保留其他层。"},
    {"Remove only this layer; remaining layers are recomputed in their original order.", "仅移除此层，按原顺序重新计算剩余层。"},
    {"NES-style rules: NRS, one NEXT, no Hold/hard drop/ghost; slow DAS and ARR. Fixed minimum entry/clear delays.", "NES 风格：NRS、单 Next、无 Hold / 硬降 / 阴影，慢 DAS / ARR、逐级重力；进块与消行延迟固定取较短值。"},
    {"Tetris Effect-style handling only: DAS 10f, ARR 2f, soft drop 20x, entry 6f and clear 30f; existing rules and goal stay.", "效应风格手感：DAS 10f、ARR 2f、软降20倍、进块6f、消行30f；保留当前规则与目标。"},
    {"150-line goal with Marathon gravity progression; keeps existing rotation, randomizer, Hold, NEXT and handling. Combines with Classic in either order.", "150行目标与马拉松递增重力；保留旋转、随机、Hold、Next 和手感，与经典块叠加不受顺序影响。"},
    {"Local Versus keeps the match mode and applies only rules and handling; the 150-line goal is single-player only.", "单窗口对战保留对战模式，应用规则与手感；150行目标仅在单人模式生效。"},
    {"No preset layers to change.", "当前没有可操作的预设层。"}, {"Preset changes saved.", "预设修改已保存。"},
    {"Confirmation cancelled.", "已取消确认。"},
    {"Press Enter again to keep current values and discard preset recovery. Arrow keys cancel.", "再按 Enter 保留当前参数并清除预设恢复记录；方向键取消。"},
    {"Press Enter again to apply this preset. Arrow keys cancel.", "再次按 Enter 确认应用此预设；方向键取消。"},
    {"Press Enter again to undo the latest preset layer. Arrow keys cancel.", "再次按 Enter 确认撤回最近一层预设；方向键取消。"},
    {"Press Enter again to undo all presets. Arrow keys cancel.", "再次按 Enter 确认撤回全部预设；方向键取消。"},
    {"Press Enter again to remove this preset layer. Arrow keys cancel.", "再次按 Enter 确认移除此层预设；方向键取消。"},
    {"All preset changes require confirmation twice.", "所有预设修改都需再次确认。"},
    {"NES progression", "NES 递增"}, {"Marathon progression", "马拉松递增"}, {"Fixed", "固定"},
    {"Gravity progression", "重力进程"}, {"Starting interval (seconds)", "起始间隔（秒）"},
    {"Gravity mode", "重力模式"},
    {"Lines per level", "每级消行数"}, {"Acceleration factor", "加速倍率"},
    {"Positive seconds between gravity steps; progressive modes use this as their starting interval.", "每下落一格的正数秒数；递增模式中作为起始间隔。"},
    {"Cleared lines required for each speed level. Default 10; independent of presets.", "每提高一级所需消行数，默认10；不依赖预设。"},
    {"1 follows the reference curve; above 1 accelerates more, below 1 accelerates less. Range 0.01-100.", "1使用参考曲线；大于1加速更强，小于1更缓，范围0.01～100。"},
    {"Enlarge the terminal until the outer frame is fully visible.", "请放大终端，直到外围框完整显示。"},
    {"Terminal size is sufficient.", "终端尺寸已足够。"},
    {"Required: ", "所需尺寸："}, {" cells; current: ", " 字符；当前尺寸："},
    {"Minimum layout: ", "最小布局："}, {"Full layout: ", "完整布局："}, {"Current size: ", "当前尺寸："},
    {"Minimum layout fits; NEXT may be clipped.", "必需信息可完整显示；Next 可能被裁剪。"},
    {"Some information will be clipped.", "部分信息将被裁剪。"},
    {"Press the restart key to start anyway.", "按重开键仍可直接开始游戏。"},
    {"Press the restart key to start the game.", "按重开键开始游戏。"},
    {"Return to start screen", "返回开始界面"},
    {"MODEL", "游戏模式"}, {"GARBAGE", "垃圾行"}, {"QUICK PLAY", "快速游戏"},
    {"HANDLING", "操作手感"}, {"GAME", "游戏机制"}, {"AUTOPLAY", "机器人"},
    {"CONTROLS", "按键绑定"}, {"NETWORK", "联机"}, {"OTHERS", "外观"}, {"RESET", "重置"},
    {"Continue input when unfocused", "失去焦点时继续输入"},
    {"Garbage rise speed (seconds)", "垃圾行涨入速度（秒）"},
    {"Seconds between rising garbage rows. 0 raises the batch instantly. Only the first row waits for the entry rule; later rows continue during movement. Quick Play uses 5/60 seconds; EX/rEX rises instantly.", "每隔这么多秒涨入一行垃圾；0为整批瞬间涨入。仅首行等待涨入条件，剩余行在落块期间继续涨入。快速游戏固定5/60秒，EX/rEX瞬间涨入。"},
    {"OFF ignores keyboard input while the game terminal is unfocused. ON continues accepting it. Shared by both players.", "关闭时，游戏终端失去焦点后忽略键盘输入；开启时仍接收输入。双方共用。"},
    {"Model", "模式"}, {"Zen", "自由游玩"}, {"Racing", "竞速"}, {"Blitz", "限时挑战"},
    {"QuickPlay", "快速游戏"}, {"Multiplayer", "在线对战"}, {"Local Versus", "单窗口对战"},
    {"Replay", "回放"}, {"Racing Distance", "目标行数"}, {"Blitz Time", "挑战时限（秒）"},
    {"Time Multiplier", "回放速度"}, {"Gravity policy", "重力变化"}, {"Increasing", "逐渐加速"}, {"Fixed", "固定速度"},
    {"Garbage Model", "垃圾行模式"}, {"None", "无"}, {"Backfire", "自攻自受"},
    {"Unclear", "不消除"}, {"Cheese-layer", "Cheese 填层"}, {"Cheese-timer", "Cheese 定时"},
    {"Survival", "生存"}, {"Garbage Shape", "垃圾行形状"}, {"Garbage shape", "垃圾行形状"},
    {"Standard holes", "标准空洞"}, {"Random density", "随机密度"}, {"Checkerboard", "棋盘格"},
    {"Cheese Messiness", "混乱概率"}, {"Density", "密度"}, {"Refill cleared lines", "补回已消行"},
    {"Layer Height", "填层高度"}, {"Time Interval", "生成间隔（秒）"},
    {"Hole pattern", "空洞配置"}, {"Layout", "空洞形状"}, {"Scattered", "分散空洞"}, {"Contiguous", "连续缺口"},
    {"Run length", "缺口宽度"}, {"Hole count", "空洞数量"}, {"Num of Hole", "空洞数量"},
    {"Garbage Multiple", "攻击倍率"}, {"GarbageInTime", "缓冲时间（秒）"}, {"APM for Survival", "生存压力 APM"},
    {"Buffer time (seconds)", "缓冲时间（秒）"}, {"Garbage entry mode", "垃圾进入模式"},
    {"After non-clear", "未消行落块"}, {"After lock", "每次落块"}, {"Immediate", "即时进入"},
    {"Garbage buffering, shape and opening cancellation protection.", "垃圾行缓冲、形状与开局抵消保护。"},
    {"Time before buffered garbage becomes an attack; 0 attacks immediately. Up to 0.3s uses dark red then bright red; longer uses yellow, dark red, then bright red.", "垃圾从缓冲阶段进入攻击阶段前的时间；0 立即进入攻击阶段。0.3 秒及以下用深红、大红；更长时依次用黄色、深红、大红。"},
    {"After non-clear waits for a non-clearing lock; After lock cancels first and raises the remaining ready garbage after any lock; Immediate raises ready garbage without waiting for a lock.", "未消行落块：仅未消行时落块后进入。每次落块：消行先抵消，落块后剩余的已就绪垃圾进入。即时进入：达到攻击阶段即进入，无需等待落块。"},
    {"Default floor", "初始楼层"}, {"Expert", "专家"}, {"Nohold", "禁用暂存"}, {"Messy", "混乱"},
    {"Gravity", "重力"}, {"Volatile", "易爆"}, {"Double hole", "双洞"}, {"Invisible", "隐形"},
    {"Allspin", "全旋"}, {"Snake eyes", "蛇眼"}, {"Snowball", "雪球"}, {"Fool", "愚人节"},
    {"Close", "关闭"}, {"Open", "开启"}, {"Reverse", "逆位"}, {"Pentr", "五连块"}, {"Hexo", "六连块"},
    {"20260401", "愚人节 2026"}, {"r20260401", "逆位愚人节 2026"}, {"OFF", "关闭"}, {"ON", "开启"},
    {"Mode", "重力模式"}, {"Periodic", "定时下落"}, {"Disabled", "关闭"}, {"Instant (20G)", "瞬间下落（20G）"},
    {"Interval (seconds)", "下落间隔（秒）"}, {"Speed", "下落间隔（秒）"},
    {"SDF", "软降 SDF"}, {"DAS", "重复延迟 DAS（秒）"}, {"ARR", "重复间隔 ARR（秒）"},
    {"DCD", "DAS 取消延迟 DCD（秒）"},
    {"Pre-move", "预移动"}, {"DAS Precharge", "DAS预充能"},
    {"Shift one cell before spawn collision and instant gravity when DAS is charged. Disabling this does not disable DAS precharge.", "DAS 已充满时，在出生碰撞和瞬间重力处理前横移一格。关闭后仍可单独开启 DAS预充能。"},
    {"Charge DAS during countdown, line clear, entry delay and single-player pause. When OFF, DAS starts from spawn; pause freezes existing charge.", "倒计时、消行、进块等待及单人暂停期间可提前充满 DAS。关闭后从进块重新计算 DAS；暂停只冻结已有充能。"},
    {"SDF Fixed", "SDF 固定间隔"},
    {"Pause charged DAS after rotation or hard drop. Seconds (no unit or s/S), or decimal frames (f/F, 60 fps). 0 disables DAS cut.", "旋转或硬降后暂停已充满 DAS 的自动移动。无单位或 s/S 为秒，f/F 为帧（60 帧/秒，支持小数帧）；0 不暂停。"},
    {"No unit: gravity multiplier (1 cell/s when gravity is disabled); s/S: fixed seconds per cell; f/F: frames at 60 fps. Uses the faster of soft drop and gravity. 0 means instant drop.", "无单位为重力倍率；无重力时以 1 格/秒为基准。s/S 为固定每格秒数，f/F 为帧数（60 帧/秒）；软降与当前重力取更快速度，0 为无穷大，立即降到底。"},
    {"Attack rules", "攻击规则"},
    {"B2B reward", "B2B奖励"},
    {"B2B attack bonus", "固定攻击奖励"},
    {"B2B reward mode", "B2B奖励模式"},
    {"Fixed attack", "固定攻击"},
    {"B2B chaining", "B2B连锁"},
    {"B2B charging", "B2B充能"},
    {"B2B charge threshold", "B2B充能下限"},
    {"B2B charge reduction", "B2B充能减伤"},
    {"Perfect clear", "全消"},
    {"Enable perfect clear", "启用全消"},
    {"Perfect clear attack", "全消攻击"},
    {"Perfect clear B2B charge", "全消B2B充能"},
    {"Half clear", "半消"},
    {"Enable half clear", "启用半消"},
    {"Half clear attack", "半消攻击"},
    {"Half clear B2B charge", "半消B2B充能"},
    {"Garbage special attack", "垃圾特殊攻击"},
    {"B2B, clear rewards and garbage special attacks.", "B2B奖励与充能、全消、半消和消除垃圾的特殊攻击。"},
    {"Choose a fixed bonus, TETR.IO chaining or no B2B reward.", "选择固定攻击、TETR.IO 连锁奖励或无 B2B 奖励。"},
    {"Choose B2B attack rewards and configure charge release when the chain breaks.", "设置 B2B 攻击奖励模式与断连时的充能释放。"},
    {"Fixed attack adds the configured bonus at displayed B2B >= 1; chaining follows TETR.IO's logarithmic bonus before combo scaling.", "固定攻击：显示的 B2B>=1 时增加设定奖励；连锁：按 TETR.IO 对数公式加成，再应用连击倍率。"},
    {"Attack bonus at displayed B2B >= 1, before combo scaling. Default 1; 0 adds no bonus.", "显示的 B2B>=1 时增加的攻击奖励，再应用连击倍率；默认 1，0 不增加奖励。"},
    {"Release stored B2B when the chain breaks.", "B2B 断连时释放已积累的充能。"},
    {"Minimum displayed B2B for a surge; default 4.", "达到该显示 B2B 值才释放浪涌；默认 4。"},
    {"Surge attack = max(0, displayed B2B - reduction).", "浪涌攻击=max(0,显示的 B2B-减伤)；减伤可为负数，负数增加攻击。"},
    {"Rewards for an empty field after clearing.", "消行后场地完全为空时获得奖励。"},
    {"Total B2B increment for this clear; 0 adds no charge.", "该次消除的 B2B 总增量；0 不增加充能。"},
    {"No placed-piece cells remain; garbage may remain. Excludes perfect clears.", "消行后没有来自放置方块的实格，可保留垃圾格；不包含全消。"},
    {"Flat bonus for a B2B-eligible clear removing garbage; not multiplied by the number of garbage rows.", "满足 B2B 条件的消除同时消除垃圾时，增加固定攻击；不按垃圾行数相乘。"},
    {"Playfield", "场地"}, {"Pieces", "方块"}, {"Timing", "锁定与计时"},
    {"Width", "场地宽度"}, {"Height", "场地高度"}, {"Anti Gravity", "反重力"},
    {"Num of Next", "预览数量"}, {"Open Hold", "允许暂存"}, {"Ghost", "落点提示"},
    {"Infinite Hold", "无限 Hold"}, {"Hold count", "Hold 次数"},
    {"Unlimited Hold uses before locking.", "每次落块前可无限使用 Hold。"},
    {"Maximum Hold uses before locking; ignored in infinite mode.", "每次落块前可用的 Hold 次数；无限模式下不限制。"},
    {"Hold uses per piece; 0 disables Hold unless Infinite Hold is ON.", "每次落块前可用的 Hold 次数；0 禁用 Hold，开启无限 Hold 时不受此限制。"},
    {"Disable Hold", "禁用 Hold"},
    {"Disable Undo", "禁用撤回"}, {"Disable Pause", "禁用暂停"}, {"Pause game", "暂停 / 恢复"},
    {"ON disables Hold; shares the Allow Hold setting in Pieces.", "开启即禁用 Hold；与“方块”中的“允许暂存”同步。"},
    {"Rotation System", "旋转系统"}, {"Random", "随机规则"}, {"Random rules", "随机规则"}, {"PENTOMINO", "五连块"}, {"HEXOMINO", "六连块"},
    {"Keep shuffling a bag of the 7 tetrominoes.", "不断洗牌，每包包含七种方块各一个。"},
    {"Keep shuffling a bag of 2x the 7 tetrominoes.", "不断洗牌，每包包含七种方块各两个。"},
    {"Keep shuffling a bag of the 7 tetrominoes with one random extra tetromino.", "不断洗牌，每包包含七种方块，另加一个随机方块。"},
    {"Keep shuffling a bag of the 7 tetrominoes with two random extra tetrominoes.", "不断洗牌，每包包含七种方块，另加两个随机方块。"},
    {"Keep shuffling a bag of the 7 tetrominoes. The first few bags contain a few extras.", "不断洗牌，每包包含七种方块，前几包另含额外方块。"},
    {"Random with repetition protection.", "随机生成，带有抑制重复的机制。"},
    {"Alternate between 2 tetrominoes.", "在两种方块之间交替生成。"},
    {"Completely random generation.", "完全随机生成。"},
    {"The default natural rotation system with symmetric I-piece rotation.", "默认自然旋转系统，I 块旋转采用对称踢墙。"},
    {"The standard natural rotation system.", "标准自然旋转系统。"},
    {"SRS with more powerful 180 spins.", "SRS 的扩展版，具有更强的 180° 旋转能力。"},
    {"Novel rotation system by Dr Ocelot.", "Dr Ocelot 设计的新式旋转系统。"},
    {"The classic rotation system.", "经典旋转系统。"},
    {"Rotation system used in arcade games.", "街机游戏使用的旋转系统。"},
    {"Permissive rotation system by Winternebs.", "Winternebs 设计的宽松旋转系统。"},
    {"No kicks possible.", "不允许踢墙。"},
    {"Spin bonuses", "旋转奖励"},
    {"Choose spin reward rules; preview each option before applying.", "选择旋转奖励规则；提示栏会说明当前选项的效果。"},
    {"Receive bonuses for spinning T-pieces.", "T 块旋转可获得奖励。"},
    {"Receive bonuses for spinning T-pieces (immobile allowed).", "T 块旋转可获得奖励（允许不可移动判定）。"},
    {"Receive bonuses for spinning all pieces.", "所有方块旋转均可获得奖励。"},
    {"Receive bonuses for spinning all pieces (immobile allowed).", "所有方块旋转均可获得奖励（允许不可移动判定）。"},
    {"Receive bonuses for spinning T-pieces and receive Back-to-Back for spinning all other pieces.", "T 块旋转可获得奖励，其他方块旋转可获得 Back-to-Back。"},
    {"Receive bonuses for spinning T-pieces (immobile allowed) and receive Back-to-Back for spinning all other pieces.", "T 块旋转可获得奖励（允许不可移动判定），其他方块旋转可获得 Back-to-Back。"},
    {"Receive Back-to-Back for spinning all pieces.", "所有方块旋转均可获得 Back-to-Back。"},
    {"Receive bonuses for spinning all pieces but attacks are halved except for T-Spins.", "所有方块旋转均可获得奖励，但除 T-Spin 外，旋转攻击减半。"},
    {"Everything is a spin because YEAH WHY NOT (O-spin SUPPORTED!).", "一切都算旋转，因为有何不可！（支持 O-Spin！）"},
    {"Receive no spin bonuses.", "旋转不获得奖励。"},
    {"EPLD", "锁定延迟（秒）"}, {"Limit of EPLD", "锁定延迟重置次数"}, {"20G", "瞬间下落（20G）"},
    {"Line Clear Delay", "消行延迟（秒）"}, {"Clock", "游戏计时"}, {"Real time", "真实时间"}, {"Per piece", "按落块推进"},
    {"Infinite movement", "无限移动"}, {"Entry delay", "出块延迟（秒）"},
    {"Hit multiplier", "受击倍率"}, {"Cancellation multiplier", "抵消倍率"},
    {"Messiness within attack", "攻击内混乱度"}, {"Avoid same column RNG", "避免随机到相同洞列"},
    {"Garbage cap", "涨入上限"}, {"Opener phase", "开局阶段（块数）"},
    {"Multiply received attacks; fractional lines carry over to the next packet. 0 discards received attacks.", "收到的攻击乘以该值；不足一行的小数累积到后续攻击。0 不接收攻击。"},
    {"Garbage generation, reception, sending and rise rules.", "垃圾行的生成、接收、发送与涨入规则。"},
    {"Allow unlimited grounded lock-delay resets; hard drop still locks immediately.", "允许无限次通过有效移动或旋转重置落地锁定计时；硬降仍立即锁定。"},
    {"Line-clear delay (seconds); 0 clears immediately.", "消行延迟时长（秒）；0 立即完成消行。"},
    {"ARE: seconds after locking and line-clear delay, before the next piece appears; 0 spawns immediately. 0.1 seconds = 6 frames at 60 fps.", "ARE：锁定并完成消行后，下一个方块出现前的等待时长（秒）；0 立即出块，0.1 秒相当于 60 FPS 下的 6 帧。"},
    {"Attack strength, hole randomness, rise limits and opener protection.", "发送攻击的强度、洞列随机规则、涨入限制与开局保护。"},
    {"Incoming lines cancelled per attack point; unused attack keeps its original strength. Fractions carry over. 0 disables cancellation.", "每点攻击可抵消的垃圾行数；剩余攻击按原强度发送，小数余量累积。0 禁止抵消。"},
    {"0-1: probability of choosing a new hole column within one attack; 0 keeps it aligned, 1 rerolls every row.", "0-1：同一次攻击中每行重新选择洞列的概率；0 保持对齐，1 每行都重新随机。"},
    {"When rerolling a garbage hole, exclude its previous column. Aligned rows can still share a hole.", "重新随机洞列时不选回原列；保持对齐的多行仍可使用同一洞列。"},
    {"Maximum ready garbage rows per rise; 0 prevents rising, but keeps queued garbage.", "每次进入场地的已就绪垃圾行上限；0 不进入，但仍保留缓冲区中的垃圾行。"},
    {"Opening piece count; 0 disables protection. Within this count, pending garbage at least equal to previously sent garbage grants extra cancellation, never extra attack.", "开局保护的方块数；0 关闭。范围内待接收垃圾量不少于累计已发送量时，获得额外抵消能力，不增加发送攻击。"},
    {"Simulation PPS", "模拟 PPS"}, {"Turn taking", "对战轮流模式"}, {"Simultaneous", "同时游玩"},
    {"Alternating", "轮流游玩"}, {"Turn Pieces", "每轮落块数"},
    {"Bot", "机器人"}, {"PPS of Autoplay", "机器人 PPS"}, {"Width of Autoplay", "搜索宽度"},
    {"Bot PPS", "机器人 PPS"}, {"Search width", "搜索宽度"}, {"Handling simulation", "手感模拟"},
    {"Handling simulation is currently supported only by bot_chatgpt.", "手感模拟目前仅支持 bot_chatgpt。"},
    {"Freeze the piece while thinking, then animate the chosen landing using DAS / ARR / DCD and soft drop pacing. 20G settles immediately. ChatGPT only, OFF by default.", "思考时方块保持不动，选点后按 DAS / ARR / DCD 和软降速度播放路径；20G 即时落底。目前仅支持 ChatGPT，默认关闭。"},
    {"Move Piece Left", "左移"}, {"Move Piece Right", "右移"}, {"Soft Drop", "软降"}, {"Hard Drop", "硬降"},
    {"Rotate Clockwise", "顺时针旋转"}, {"Rotate Counterclockwise", "逆时针旋转"}, {"Rotate 180", "旋转 180°"},
    {"Swap Hold Piece", "暂存 / 交换"}, {"Retry Game", "重新开始"}, {"Open Config", "打开配置"}, {"Undo", "撤销"},
    {"Server IP", "服务器地址"}, {"CONNECT / RECONNECT", "连接 / 重连"}, {"DISCONNECT", "断开连接"},
    {"Square Version", "方块样式"}, {"ObscuredColors", "隐藏颜色"}, {"Filled", "实心"}, {"Spaced", "间隔"}, {"Custom", "自定义"},
    {"Interface language", "界面语言"},
    {"Edit target", "修改对象"}, {"Edit target: ", "修改对象："},
    {"Both players", "双方"}, {"Neither player", "双方均未启用"},
    {"Edit the selected parameters for both players.", "仅将当前编辑的参数同时应用于双方。"},
    {"Choose Player 1, Player 2 or Both players; Tab cycles targets without leaving the current item.", "选择玩家1、玩家2或双方；Tab 循环切换对象并保留当前条目。"},
    {"Choose Player 1 or Player 2 to edit this item.", "此项需切换到玩家1或玩家2后编辑。"},
    {"Copy other player's settings", "复制另一名玩家的配置"},
    {"Copy the other player's full configuration into the selected player, keeping this player's keyboard bindings. Confirm twice.", "将另一名玩家的全部配置复制到当前玩家，保留当前玩家的键位；需再次确认。"},
    {"Press Enter again to copy the other player's settings. Arrow keys cancel.", "再次按 Enter 确认复制另一名玩家的配置；方向键取消。"},
    {"Other player's settings copied. Changes saved.", "已复制另一名玩家的配置并保存。"},
    {"Currently active: ", "当前生效："},
    {"Different values: choose one value for both players.", "两边值不同，请选择双方共同的新值。"},
    {"[Tab] Player 1 / Player 2 / Both players", "[Tab] 玩家1 / 玩家2 / 双方"},
    {"Press Enter again to reset both players. Arrow keys cancel.", "再按 Enter 确认同时重置双方；方向键取消。"},
    {"Show spawn position", "显示出生位置"},
    {"Mark the next piece's normal spawn cells with light dots in empty cells and small holes in blocks; requires NEXT and ignores Clutch.",
     "以空白格中的灰白点、方块中的小暗孔标记下一块的正常出生位置；需有 NEXT，不计 Clutch。"},
    {"Choose Chinese or English; applies to both players and the start screen.", "选择中英文；应用于双方界面与开始界面。"},
    {"Space", "空格"}, {"Up", "↑"}, {"Down", "↓"}, {"Left", "←"}, {"Right", "→"}, {"Key ", "键码 "},
    {" [current]", " [当前]"}, {"Edit ", "编辑 "}, {"Select ", "选择 "},
    {"Current: ", "当前值："}, {"New value: ", "新值："},
    {"Key: ", "按键："}, {"Unbound", "未绑定"},
    {"Press a key to add, transfer, or remove its binding.", "按键：新增绑定、从其他功能转移，或移除已有绑定。"},
    {"Press and release a key; existing bindings toggle.", "按下并松开按键；已有绑定会移除。"},
    {"Up/Down Select   Right/Enter Apply", "↑↓ 选择   → / Enter 确认"},
    {"Up/Down Select   Right/Enter Open", "↑↓ 选择   → / Enter 进入"},
    {"Left/Backspace Back   [R] Restart   Esc Start screen", "← / Backspace 返回   [R] 重开   Esc 开始界面"},
    {"[P] Root", "[P] 根目录"}, {"Esc Start screen", "Esc 开始界面"},
    {"Root", "根目录"}, {"Left/Backspace Back   Esc Start screen", "← / Backspace 返回   Esc 开始界面"},
    {"Restart / Settings cannot use Enter, Esc, arrows or Tab.", "重开键 / 配置键不能绑定 Enter、Esc、四个方向键或 Tab，避免菜单冲突。"},
    {"Enter, Esc, arrows and Tab can be bound to this action.", "此动作可绑定 Enter、Esc、四个方向键和 Tab。"},
    {"Text input supports Chinese; Enter saves.", "支持中文输入；Enter 保存。"},
    {"Enter Save   Blank Keep value", "Enter 保存   留空保留"},
    {"Enter Save   Esc Save and start screen   Blank Keep value", "Enter 保存  Esc 保存并回到开始界面  留空保留"},
    {"Left/Right Cursor   Backspace/Delete Edit", "←/→ 移动光标  Backspace/Delete 编辑"},
    {"Left/Right Cursor   Backspace/Delete Edit   Esc Start screen", "←/→ 移动光标  Backspace/Delete 编辑  Esc 开始界面"},
    {"Changes apply after confirmation; Left / Backspace goes back.", "确认后应用修改；← / Backspace 返回上层。"},
    {"Choose a category; Right opens, Left goes back.", "选择类别；→ 进入，← 返回。"},
    {"Choose the player whose settings you are editing.", "选择要编辑配置的玩家。"},
    {"Edit Player 1's independent settings.", "编辑玩家 1 的独立配置。"},
    {"Edit Player 2's independent settings.", "编辑玩家 2 的独立配置。"},
    {"Only Player 1 is available outside Local Versus.", "单窗口对战关闭时，仅显示玩家 1。"},
    {"Right: choose Player 1 or Player 2.", "→ 选择玩家 1 或玩家 2。"},
    {"Player 1 only; Local Versus enables Player 2.", "当前仅玩家 1；单窗口对战可启用玩家 2。"},
    {"Choose a mode; its configuration appears below.", "选择模式后，下方显示相关参数。"},
    {"Game mode and the options used by that mode.", "游戏模式及其相关参数。"},
    {"Fixed keeps Blitz gravity from increasing.", "固定速度：限时挑战中重力不会加速。"},
    {"Choose garbage behavior; relevant controls appear below.", "选择垃圾行模式后，显示相关配置。"},
    {"Garbage shape and generation behavior.", "垃圾行的图案、空洞与生成参数。"},
    {"Applies to ordinary garbage from every source; modifiers with their own shape take priority.", "用于所有来源的普通垃圾行；拥有专用形状的特殊机制优先使用各自的规则。"},
    {"0-1: probability of an occupied cell, not hole count.", "0～1：每格被填满的概率。"},
    {"0-1: probability of changing hole positions per row.", "0～1：每行改变空洞位置的概率。"},
    {"ON replenishes cleared cheese lines; OFF uses a finite layer.", "开启：补回已消行；关闭：只生成有限填层。"},
    {"Seconds between generated cheese rows.", "两次生成 Cheese 行之间的秒数。"},
    {"1-10, default 1: Quick Play starts at this floor's minimum altitude; April Fools 2026 rotation uses special pieces up to this floor.", "1～10，默认1：快速游戏从此层最低海拔开始；愚人节2026旋转系统仅加入第1层至此层特殊块。"},
    {"Challenge duration in seconds; 0 ends the game immediately.", "挑战时限（秒）；0 会立即结束游戏。"},
    {"Cheese modes: hole-change probability per row, from 0 to 1. Other modes use their attack messiness rules.", "Cheese 模式每行改变空洞位置的概率，范围 0～1；其他模式使用各自的攻击混乱规则。"},
    {"0-1: occupied-cell probability; 0 makes empty rows, 1 fills all but one hole.", "0～1：每格被填满的概率；0 生成空行，1 填满后留一个空洞。"},
    {"Seconds between cheese rows; 0 adds a row each update, or each piece with the Per piece clock.", "生成 Cheese 行的间隔（秒）；0 每次更新生成一行，按落块计时则每块生成一行。"},
    {"Initial cheese height; 0 creates no initial garbage rows.", "初始 Cheese 填层高度；0 不生成初始垃圾行。"},
    {"Attack multiplier; 0 produces no attack garbage.", "攻击倍率；0 不产生攻击垃圾行。"},
    {"Incoming garbage wait in seconds; 0 removes the wait.", "垃圾行涨入前的等待（秒）；0 取消等待。"},
    {"Survival pressure in attacks per minute; 0 generates no survival garbage.", "生存压力 APM；0 不生成生存垃圾行。"},
    {"Soft-drop speed multiplier; 0 drops instantly without locking.", "软降速度倍率；0 瞬间落到底部，仍可继续操作。"},
    {"Seconds (no unit or s/S), or decimal frames (f/F, 60 fps): 1.5f = 0.025s. 0 repeats immediately.", "DAS 重复延迟：无单位或 s/S 为秒，f/F 为帧，支持小数帧（60 帧/秒）；1.5f = 0.025 秒，0 立即重复移动。"},
    {"Seconds (no unit or s/S), or decimal frames (f/F, 60 fps): 1.5f = 0.025s. 0 moves to the wall after DAS.", "ARR 重复间隔：无单位或 s/S 为秒，f/F 为帧，支持小数帧（60 帧/秒）；1.5f = 0.025 秒，0 在 DAS 后移到墙边。"},
    {"Number of preview pieces; 0 hides NEXT.", "预览方块数量；0 隐藏 NEXT。"},
    {"Grounded lock delay (seconds); 0 locks immediately.", "触地后的锁定延迟（秒）；0 立即锁定。"},
    {"Maximum lock-delay resets per piece; 0 allows no resets.", "每块可重置锁定延迟的次数；0 不允许重置。"},
    {"Line-clear animation duration (seconds); 0 skips the animation.", "消行延迟时长（秒）；0 跳过动画。"},
    {"Bot pieces per second; 0 runs without a speed limit.", "机器人每秒落块数；0 不限制落块速度。"},
    {"Set hole shape separately from its size.", "分别设置空洞形状与数量 / 宽度。"},
    {"Contiguous: one gap with a configurable run length.", "连续缺口：一个缺口，可设置其宽度。"},
    {"Scattered: independently placed open columns.", "分散空洞：空列分别分布。"},
    {"Positive width of the contiguous gap.", "连续缺口的宽度；输入正数。"},
    {"Number of open columns, limited by field width.", "空列数量；范围为 1～场地宽度。"},
    {"Garbage options follow the selected garbage model.", "垃圾行配置随当前模式变化。"},
    {"Quick Play modifiers; Right / Enter cycles each value.", "快速游戏 Mod；→ / Enter 切换取值。"},
    {"Choose gravity behavior before setting its interval.", "先选择重力模式，再设置下落间隔。"},
    {"Gravity modes use words; no signed or sentinel numbers.", "直接选择定时、关闭或瞬间下落。"},
    {"Positive seconds between gravity steps.", "每次自然下落之间的秒数；输入正数。"},
    {"Delay before held movement repeats (seconds).", "按住方向键后，开始重复移动的延迟（秒）。"},
    {"Interval between repeated moves (seconds).", "按住方向键时，重复移动的间隔（秒）。"},
    {"Soft drop and movement repeat controls.", "软降与方向键重复手感。"},
    {"Field dimensions and visibility.", "场地尺寸、隐形与反重力。"},
    {"Next, hold, ghost and piece rules.", "预览、暂存、落点与方块规则。"},
    {"Locking, entry delays and game-clock behavior.", "锁定、消行与出块延迟、计时与轮流规则。"},
    {"Per piece advances game time only after a placement.", "按落块推进：每完成一块才推进游戏时间。"},
    {"Positive pieces per game second; movement uses real time.", "每游戏秒的落块数；操作仍使用真实时间。"},
    {"Alternating requires a positive quota for both players.", "轮流游玩需为双方设置正数配额。"},
    {"Positive pieces per turn; fractional quotas are allowed.", "每轮允许落块数；可输入正小数。"},
    {"Field, piece rules, gravity, and timing options.", "场地、方块、重力、锁定与计时规则。"},
    {"Bot selection; tuning appears when a bot is enabled.", "选择机器人后显示速度与搜索参数。"},
    {"Keyboard bindings; select an action to capture a key.", "选择操作后，按下并松开要绑定的键。"},
    {"Connect to this server and open the lobby.", "连接此服务器并进入联机大厅。"},
    {"Connect to this server and show connection status.", "连接此服务器，在右侧显示连接状态。"},
    {"Enter Confirm   Blank Auto name", "Enter 确认；创建时留空自动生成房号"},
    {"Username", "用户名"}, {"You", "你"},
    {"Change your username before or after connecting; online changes sync with the server.", "连接服务器前后均可修改用户名；联机修改会同步到服务器。"},
    {"Rooms", "房间"},
    {"Create room", "创建房间"},
    {"Join by name", "按名称加入"},
    {"Room lobby", "房间大厅"},
    {"Room name", "房间名"},
    {"Room type", "房间类型"},
    {"Public", "公开"},
    {"Private", "私有"},
    {"Empty", "空"},
    {"Join room", "加入房间"},
    {"Refresh rooms", "刷新房间"},
    {"Current room", "当前房间"},
    {"Transfer host", "切换房主"},
    {"Force host configuration", "强制使用房主配置"},
    {"Force host handling", "强制使用房主手感"},
    {"Host configuration", "房主配置"},
    {"Host handling", "房主手感"},
    {"Select a player to transfer host immediately.", "选择房间成员并赋予房主，自己随即取消房主身份。"},
    {"Apply host handling before starting. Keyboard bindings stay local.", "开始前统一使用房主的操作手感，键位仍使用自己的配置。"},
    {"Apply host rules, map and piece sequence before starting. Appearance and handling stay local.", "开始前统一规则、地图和块序，外观与手感仍使用自己的配置。"},
    {"Host transferred.", "房主已转让。"},
    {"Host configuration policy updated.", "房主配置开关已更新。"},
    {"Only the room host can change this option.", "只有房主可以修改此选项。"},
    {"Player is no longer in this room. Select another player.", "该玩家已离开房间，请重新选择。"},
    {"Return to the room before changing the host or configuration policy.", "请先返回房间，再切换房主或配置开关。"},
    {"Update all players before using forced host configuration.", "请所有玩家更新客户端，再使用房主强制配置。"},
    {"Host configuration transfer timed out. Start again.", "房主配置传输超时，请重新开始。"},
    {"Leave room", "退出房间"},
    {"Create or join a room before starting a game.", "请先创建或加入房间，再开始对战。"},
    {"Connection status updates here. Choose Rooms to create or join a room.", "连接状态在此更新；选择房间以创建或加入房间。"},
    {"Connect to the server before choosing a room.", "请先连接服务器，再选择房间。"},
    {"Blank name generates four random uppercase letters.", "房间名为空时，自动生成四个随机大写字母作为房号。"},
    {"Enter the exact room name, including private rooms.", "输入完整房间名，可加入公开或私有房间。"},
    {"Select this room to join.", "选择此房间并加入。"},
    {"No public rooms.", "暂无公开房间。"},
    {"Start only after creating or joining a room.", "创建或加入房间后才可开始对战。"},
    {"Room created. Choose Start game when ready.", "房间已创建；准备好后选择开始对战。"},
    {"Joined room. Choose Start game when ready.", "已加入房间；准备好后选择开始对战。"},
    {"Public rooms: select a room to join.", "公开房间：选择房间以加入。"},
    {"Left room.", "已退出房间。"},
    {"Room name already exists. Enter a different name.", "房间名已被创建，请更改名字。"},
    {"Room does not exist. Enter the name again.", "房间不存在，请重新输入房间名。"},
    {"Room is full (2 players). Choose another room.", "房间已满（2名玩家），请选择其他房间。"},
    {"Invalid room name. Use 1-48 UTF-8 bytes without control characters.", "房间名无效；限48个UTF-8字节，不能含控制字符。"},
    {"Leave the current room before creating or joining another.", "请先退出当前房间，再创建或加入其他房间。"},
    {"Server busy. Try again later.", "服务器繁忙，请稍后重试。"},
    {"Waiting for the server...", "正在等待服务器响应……"},
    {"Room request timed out. Refresh or try again.", "房间请求超时，请刷新或重试。"},
    {"Server connection limit reached.", "服务器连接数已达上限。"},
    {"Connection status", "连接状态"}, {"Start game", "开始对战"}, {"Back to settings", "返回设置"},
    {"Connection status updates here. Choose Start game to enter the lobby.", "连接状态在此更新；选择开始对战后进入联机大厅。"},
    {"Connect to the server before starting a game.", "请先连接服务器，再开始对战。"},
    {"Disconnect from the server.", "断开当前服务器连接。"},
    {"Server address and connection actions.", "服务器地址、连接与断开操作。"},
    {"Shared by both players: block appearance, color visibility, and interface language.", "双方共用方块样式、隐藏颜色与界面语言。"},
    {"Clear presets; keep handling, appearance, network, language and keyboard bindings. Multiplayer mode is preserved.", "重置游戏配置并清空预设；保留操作手感、外观、网络选项、语言和键位，联机模式不变。"},
    {"Root reset keeps Local Versus mode.", "根菜单重置保留单窗口对战模式。"},
    {"Reset only this category; confirm twice.", "仅重置此类别；需再次确认。"},
    {"Reset only this submenu; confirm twice.", "仅重置此子菜单；需再次确认。"},
    {"Reset the current player's game settings and presets; keep appearance, language and bindings. Confirm twice.", "重置当前玩家游戏配置并清空预设，保留外观、语言和键位；需再次确认。"},
    {"Enter a new key binding; Esc cancels.", "按下新按键进行绑定；Esc 取消。"},
    {"Right / Enter cycles the value.", "→ / Enter 切换取值。"},
    {"Right / Enter opens the available values.", "→ / Enter 打开可选值。"},
    {"Replay filename or path inside replay/.", "输入 replay/ 内的回放文件名，支持中文。"},
    {"Press Save Replay ", "按保存回放键"},
    {"Press either player's Save Replay ", "按任一玩家的保存回放键"},
    {" to save this game.", "保存本局回放。"},
    {"Replay save cancelled. Press Save Replay ", "已取消保存，按保存回放键"},
    {"Replay save cancelled. Press either player's Save Replay ", "已取消保存，按任一玩家的保存回放键"},
    {" to try again.", "可重新保存。"},
    {"Replay file [replay/", "回放文件 [replay/"},
    {"Enter: default name   /cancel: cancel", "Enter：使用默认名  /cancel：取消"},
    {"Enter: default name   Esc / /cancel: cancel", "Enter：使用默认名  Esc / /cancel：取消"},
    {"Custom replay name: ", "自定义名称："},
    {"] (Enter=default, /cancel=discard): ", "]（Enter 使用默认名，/cancel 取消）："},
    {"Invalid file name. Replays must stay inside replay/.", "文件名无效，回放必须保存在 replay/ 内。"},
    {"Could not save the replay.", "回放保存失败。"},
    {"The replay was too long to save.", "回放过长，无法保存。"},
    {"Replay saved or disabled.", "回放已保存或无法保存。"},
    {"Replay saved: ", "回放已保存："},
    {"Right / Enter edits; blank input keeps the value.", "→ / Enter 编辑；留空保留当前值。"},
    {"Inactive with the current rules; edits are saved for other configurations.", "当前规则下不生效；修改会保存，供其他配置使用。"},
    {"Text input supports Chinese; Enter saves, Esc cancels.", "支持中文输入；Enter 保存，Esc 取消。"},
    {"Up/Down Select   Right/Enter Apply   Left/Esc Cancel", "↑↓ 选择   → / Enter 确认   ← / Esc 取消"},
    {"Up/Down Select   Right/Enter Open   Left/Esc Back", "↑↓ 选择   → / Enter 进入   ← / Esc 返回"},
    {" Root   [", " 根目录   ["}, {" Restart   Esc ", " 重开   Esc "}, {" Restart", " 重开"},
    {" Return to start   Esc ", " 开始界面   Esc "}, {" Return to start", " 开始界面"},
    {"Restart", "重开"}, {"Back", "返回"}, {"Cancel", "取消"}, {"Start screen", "开始界面"},
    {"   PgUp/PgDn Page", "   PgUp/PgDn 翻页"},
    {"Press and release a new key. Esc cancels.", "按下并松开新按键；Esc 取消。"},
    {"New key binding takes effect after capture.", "捕获完成后立即应用新按键。"},
    {"Enter Save   Blank Keep value   Left/Esc Cancel", "Enter 保存   留空保留   ← / Esc 取消"},
    {"Enter Save   Blank Keep value   Esc Cancel", "Enter 保存   留空保留   Esc 取消"},
    {"Changes apply after confirmation; Left / Esc cancels.", "确认后应用修改；← / Esc 取消。"},
    {"Press Enter again to confirm RESET. Arrow keys cancel.", "再次按 Enter 确认重置；方向键取消。"},
    {"RESET cancelled.", "已取消重置。"}, {"RESET complete. Changes saved.", "重置完成，配置已保存。"},
    {"Disabled movement", "禁用移动"},
    {"Disabled actions", "禁用操作"},
    {"ON disables the action; gravity continues normally.", "开启即禁止对应动作；自然重力不受影响。"},
    {"Unavailable: ", "不可用："},
    {"Requires a field width of 10.", "此机器人要求场地宽度为 10。"},
    {"Requires a field height of 20.", "此机器人要求场地高度为 20。"},
    {"Requires a field height of at most 24.", "此机器人要求场地高度不超过 24。"},
    {"Anti-gravity is unsupported.", "此机器人不支持反重力。"},
    {"SRS+ rotation system is required.", "此机器人要求 SRS+ 旋转系统。"},
    {"DAS must be greater than zero.", "此机器人要求 DAS 大于零。"},
    {"Instant gravity (20G) is unsupported.", "此机器人不支持瞬间下落（20G）。"},
    {"Bots support at most one hold per piece.", "机器人暂不支持无限暂存或每块多次暂存。"},
    {"This bot requires ALL-MINI+ spin bonuses.", "此机器人仅支持 ALL-MINI+ 旋转奖励。"},
    {"This bot requires a cancellation multiplier of 1.", "此机器人仅支持抵消倍率为 1。"},
    {"Requires at most 18 preview pieces.", "此机器人最多支持 18 个预览方块。"},
    {"This bot cannot forecast garbage with entry or line-clear delay.", "此机器人无法预测进块或消行延迟期间的垃圾行。"},
    {"This bot requires the 7-BAG randomizer.", "此机器人仅支持 7-BAG 随机器。"},
    {"This bot cannot model Quick Play's changing attack and field rules.", "此机器人不支持快速游戏的动态攻击与场地规则。"},
    {"This bot does not model the active Quick Play field modifiers.", "此机器人不支持当前快速游戏场地 Mod。"},
    {"This bot cannot handle Quick Play's gravity progression.", "此机器人不支持快速游戏的重力增长。"},
    {"This bot does not model the active Quick Play piece and attack modifiers.", "此机器人不支持当前快速游戏方块与攻击 Mod。"},
    {"Requires at least 5 preview pieces.", "此机器人要求至少 5 个预览方块。"},
    {"Requires at least 1 preview piece.", "此机器人要求至少 1 个预览方块。"},
    {"Hold must be enabled.", "此机器人要求开启暂存。"},
    {"Random pieces are unsupported.", "此机器人不支持随机方块。"},
    {"Nohold Mod is unsupported.", "此机器人不支持 Nohold Mod。"},
    {"Messy Mod is unsupported.", "此机器人不支持 Messy Mod。"},
    {"Allspin Mod is unsupported.", "此机器人不支持 Allspin Mod。"},
    {"Fool Mod is unsupported.", "此机器人不支持 Fool Mod。"},
    {"Left movement cannot be disabled.", "此机器人不支持禁用左移。"},
    {"Right movement cannot be disabled.", "此机器人不支持禁用右移。"},
    {"CW rotation cannot be disabled.", "此机器人不支持禁用顺时针旋转。"},
    {"CCW rotation cannot be disabled.", "此机器人不支持禁用逆时针旋转。"},
    {"180-degree rotation cannot be disabled.", "此机器人不支持禁用 180 度旋转。"},
    {"Soft drop cannot be disabled.", "此机器人不支持禁用软降。"},
    {"Bots require hard drop for locking.", "机器人依赖硬降锁定方块，请允许硬降。"},
    {"Unknown bot.", "未知机器人。"},
    {"Invalid value; the previous value is unchanged.", "输入无效，已保留原值。"},
    {"Could not save settings. Check the settings directory.", "保存失败，请检查配置目录。"},
    {"Disconnected", "未连接"}, {"Connecting to ", "正在连接 "}, {"Connected to ", "已连接 "},
    {"Server unavailable. Reconnect or edit Server IP.", "服务器不可用，请重连或修改地址。"},
    {"Server full (2 players).", "服务器已满（最多 2 人）。"},
    {"Connection expired. Reconnect to the server.", "连接已失效，请重新连接。"},
    {"WSAStartup failed", "网络服务初始化失败。"},
    {"[constructor] cannot create UDP socket", "无法创建 UDP 连接。"},
    {"[set_non_blocking] socket is closed", "网络连接已关闭。"},
    {"[set_non_blocking] fcntl failed", "无法设置网络非阻塞模式。"},
    {"[set_non_blocking] ioctlsocket failed", "无法设置网络非阻塞模式。"},
    {"[send] socket is closed", "网络连接已关闭。"},
    {"[send] invalid IPv4 address", "服务器地址无效，请输入 IPv4 地址。"},
    {"[resolve] invalid server address", "服务器地址无效，请输入 IPv4、域名[:端口] 或 ws:// / wss:// 地址。"},
    {"[resolve] invalid server port", "服务器端口无效，请输入 1 到 65535 的整数。"},
    {"[resolve] cannot resolve server IPv4 address", "无法解析服务器 IPv4 地址，请检查域名、A 记录和网络。"},
    {"WebSocket initialization failed.", "WebSocket 网络初始化失败。"},
    {"Cannot connect to WebSocket server.", "无法连接 WebSocket 服务器，请检查地址和网络。"},
    {"WebSocket TLS verification failed.", "WebSocket 服务器证书验证失败。"},
    {"WebSocket server disconnected.", "WebSocket 服务器连接已断开，请重新连接。"},
    {"Invalid WebSocket message.", "WebSocket 服务器消息无效或过大。"},
    {"Too much pending WebSocket data.", "WebSocket 数据积压过多，请重新连接。"},
    {"Invalid WebSocket URL.", "WebSocket 地址无效，请使用 ws:// 或 wss://。"},
    {"[send] sendto failed", "网络发送失败，请重新连接。"},
    {"[receive] socket is closed", "网络连接已关闭。"},
    {"[receive] recvfrom failed", "网络接收失败，请重新连接。"},
    {"[receive_for] select failed", "网络接收等待失败，请重新连接。"},
    {"Start playing", "开始游玩"}, {"Configure", "配置"}, {"Quit", "退出"},
    {"Start", "开始"}, {"Settings", "设置"},
    {"Build your own rules, one piece at a time.", "在终端里，拼出你的规则。"},
    {"Terminal / C++ / Custom rules", "终端 / C++ / 高度自定义"},
    {"Mode: ", "模式："}, {"Field: ", "场地："}, {"Bot: ", "机器人："},
    {"Play with the current configuration.", "使用当前配置开始游戏。"},
    {"Adjust rules, handling, keys, and player settings.", "调整规则、操作手感、按键与玩家配置。"},
    {"Exit and return to the terminal.", "退出游戏，返回终端。"},
    {"Press retry to play, or settings to configure.", "按重开键开始游玩，按设置键进入配置。"},
    {"Settings saved automatically", "配置修改自动保存"}
};
}
std::string key_label(int key, Input::KeyPlatform platform) {
    if (key < 0) return tr("Unbound");
    struct NamedKey { int code; std::string_view name; };
    // Win32 virtual-key codes, including distinct left/right modifiers.
    // Kept independent of windows.h so the Windows table is tested on Linux.
    static constexpr NamedKey windowsKeys[] = {
        {0x08, "Backspace"},
        {0x09, "Tab"},
        {0x0c, "Clear"},
        {0x0d, "Enter"},
        {0x10, "Shift"},
        {0x11, "Ctrl"},
        {0x12, "Alt"},
        {0x13, "Pause"},
        {0x14, "CapsLock"},
        {0x15, "Kana/Hangul"},
        {0x19, "Hanja/Kanji"},
        {0x1b, "Esc"},
        {0x1c, "Convert"},
        {0x1d, "NonConvert"},
        {0x20, "Space"},
        {0x21, "PgUp"},
        {0x22, "PgDn"},
        {0x23, "End"},
        {0x24, "Home"},
        {0x25, "Left"},
        {0x26, "Up"},
        {0x27, "Right"},
        {0x28, "Down"},
        {0x29, "Select"},
        {0x2a, "Print"},
        {0x2b, "Execute"},
        {0x2c, "PrintScreen"},
        {0x2d, "Insert"},
        {0x2e, "Delete"},
        {0x2f, "Help"},
        {0x5b, "LWin"},
        {0x5c, "RWin"},
        {0x5d, "Menu"},
        {0x5f, "Sleep"},
        {0x6a, "Numpad *"},
        {0x6b, "Numpad +"},
        {0x6c, "Numpad Separator"},
        {0x6d, "Numpad -"},
        {0x6e, "Numpad ."},
        {0x6f, "Numpad /"},
        {0x90, "NumLock"},
        {0x91, "ScrollLock"},
        {0xa0, "LShift"},
        {0xa1, "RShift"},
        {0xa2, "LCtrl"},
        {0xa3, "RCtrl"},
        {0xa4, "LAlt"},
        {0xa5, "RAlt"},
        {0xa6, "Browser Back"},
        {0xa7, "Browser Forward"},
        {0xa8, "Browser Refresh"},
        {0xa9, "Browser Stop"},
        {0xaa, "Browser Search"},
        {0xab, "Browser Favorites"},
        {0xac, "Browser Home"},
        {0xad, "Mute"},
        {0xae, "Volume -"},
        {0xaf, "Volume +"},
        {0xb0, "Media Next"},
        {0xb1, "Media Previous"},
        {0xb2, "Media Stop"},
        {0xb3, "Media Play/Pause"},
        {0xb4, "Mail"},
        {0xb5, "Media"},
        {0xb6, "App 1"},
        {0xb7, "App 2"},
        {0xba, ";"},
        {0xbb, "="},
        {0xbc, ","},
        {0xbd, "-"},
        {0xbe, "."},
        {0xbf, "/"},
        {0xc0, "`"},
        {0xdb, "["},
        {0xdc, "\\"},
        {0xdd, "]"},
        {0xde, "'"},
        {0xdf, "OEM 8"},
        {0xe2, "Intl \\"},
        {0xe5, "IME Process"},
        {0xe7, "Unicode Packet"},
        {0xf6, "Attn"},
        {0xf7, "CrSel"},
        {0xf8, "ExSel"},
        {0xf9, "Erase EOF"},
        {0xfa, "Play"},
        {0xfb, "Zoom"},
        {0xfd, "PA1"},
        {0xfe, "OEM Clear"},
    };
    static constexpr NamedKey evdevKeys[] = {
        {1, "Esc"}, // KEY_ESC
        {28, "Enter"}, // KEY_ENTER
        {96, "Numpad Enter"}, // KEY_KPENTER
        {14, "Backspace"}, // KEY_BACKSPACE
        {15, "Tab"}, // KEY_TAB
        {58, "CapsLock"}, // KEY_CAPSLOCK
        {57, "Space"}, // KEY_SPACE
        {42, "LShift"}, // KEY_LEFTSHIFT
        {54, "RShift"}, // KEY_RIGHTSHIFT
        {29, "LCtrl"}, // KEY_LEFTCTRL
        {97, "RCtrl"}, // KEY_RIGHTCTRL
        {56, "LAlt"}, // KEY_LEFTALT
        {100, "RAlt"}, // KEY_RIGHTALT
        {125, "LWin"}, // KEY_LEFTMETA
        {126, "RWin"}, // KEY_RIGHTMETA
        {127, "Menu"}, // KEY_COMPOSE
        {139, "Menu"}, // KEY_MENU
        {119, "Pause"}, // KEY_PAUSE
        {99, "PrintScreen"}, // KEY_SYSRQ
        {70, "ScrollLock"}, // KEY_SCROLLLOCK
        {69, "NumLock"}, // KEY_NUMLOCK
        {110, "Insert"}, // KEY_INSERT
        {111, "Delete"}, // KEY_DELETE
        {102, "Home"}, // KEY_HOME
        {107, "End"}, // KEY_END
        {104, "PgUp"}, // KEY_PAGEUP
        {109, "PgDn"}, // KEY_PAGEDOWN
        {105, "Left"}, // KEY_LEFT
        {106, "Right"}, // KEY_RIGHT
        {103, "Up"}, // KEY_UP
        {108, "Down"}, // KEY_DOWN
        {12, "-"}, // KEY_MINUS
        {13, "="}, // KEY_EQUAL
        {26, "["}, // KEY_LEFTBRACE
        {27, "]"}, // KEY_RIGHTBRACE
        {43, "\\"}, // KEY_BACKSLASH
        {39, ";"}, // KEY_SEMICOLON
        {40, "'"}, // KEY_APOSTROPHE
        {41, "`"}, // KEY_GRAVE
        {51, ","}, // KEY_COMMA
        {52, "."}, // KEY_DOT
        {53, "/"}, // KEY_SLASH
        {98, "Numpad /"}, // KEY_KPSLASH
        {55, "Numpad *"}, // KEY_KPASTERISK
        {74, "Numpad -"}, // KEY_KPMINUS
        {78, "Numpad +"}, // KEY_KPPLUS
        {83, "Numpad ."}, // KEY_KPDOT
        {117, "Numpad ="}, // KEY_KPEQUAL
        {118, "Numpad +/-"}, // KEY_KPPLUSMINUS
        {121, "Numpad ,"}, // KEY_KPCOMMA
        {179, "Numpad ("}, // KEY_KPLEFTPAREN
        {180, "Numpad )"}, // KEY_KPRIGHTPAREN
        {86, "Intl \\"}, // KEY_102ND
        {89, "Intl Ro"}, // KEY_RO
        {124, "Yen"}, // KEY_YEN
        {90, "Katakana"}, // KEY_KATAKANA
        {91, "Hiragana"}, // KEY_HIRAGANA
        {92, "Convert"}, // KEY_HENKAN
        {94, "NonConvert"}, // KEY_MUHENKAN
        {93, "Kana"}, // KEY_KATAKANAHIRAGANA
        {122, "Hangul"}, // KEY_HANGEUL
        {123, "Hanja"}, // KEY_HANJA
        {85, "Zenkaku/Hankaku"}, // KEY_ZENKAKUHANKAKU
        {113, "Mute"}, // KEY_MUTE
        {114, "Volume -"}, // KEY_VOLUMEDOWN
        {115, "Volume +"}, // KEY_VOLUMEUP
        {163, "Media Next"}, // KEY_NEXTSONG
        {165, "Media Previous"}, // KEY_PREVIOUSSONG
        {166, "Media Stop"}, // KEY_STOPCD
        {164, "Media Play/Pause"}, // KEY_PLAYPAUSE
        {200, "Media Play"}, // KEY_PLAYCD
        {201, "Media Pause"}, // KEY_PAUSECD
        {161, "Eject"}, // KEY_EJECTCD
        {155, "Mail"}, // KEY_MAIL
        {150, "Browser"}, // KEY_WWW
        {172, "Browser Home"}, // KEY_HOMEPAGE
        {158, "Browser Back"}, // KEY_BACK
        {159, "Browser Forward"}, // KEY_FORWARD
        {173, "Browser Refresh"}, // KEY_REFRESH
        {128, "Stop"}, // KEY_STOP
        {217, "Search"}, // KEY_SEARCH
        {156, "Browser Favorites"}, // KEY_BOOKMARKS
        {140, "Calculator"}, // KEY_CALC
        {157, "Computer"}, // KEY_COMPUTER
        {142, "Sleep"}, // KEY_SLEEP
        {116, "Power"}, // KEY_POWER
        {143, "Wake"}, // KEY_WAKEUP
        {138, "Help"}, // KEY_HELP
        {148, "App 1"}, // KEY_PROG1
        {149, "App 2"}, // KEY_PROG2
        {131, "Undo"}, // KEY_UNDO
        {182, "Redo"}, // KEY_REDO
        {137, "Cut"}, // KEY_CUT
        {133, "Copy"}, // KEY_COPY
        {135, "Paste"}, // KEY_PASTE
        {136, "Find"}, // KEY_FIND
        {134, "Open"}, // KEY_OPEN
        {353, "Select"}, // KEY_SELECT
        {223, "Cancel"}, // KEY_CANCEL
        {82, "Numpad 0"}, // KEY_KP0
        {79, "Numpad 1"}, // KEY_KP1
        {80, "Numpad 2"}, // KEY_KP2
        {81, "Numpad 3"}, // KEY_KP3
        {75, "Numpad 4"}, // KEY_KP4
        {76, "Numpad 5"}, // KEY_KP5
        {77, "Numpad 6"}, // KEY_KP6
        {71, "Numpad 7"}, // KEY_KP7
        {72, "Numpad 8"}, // KEY_KP8
        {73, "Numpad 9"}, // KEY_KP9
        {11, "0"}, // KEY_0
        {2, "1"}, // KEY_1
        {3, "2"}, // KEY_2
        {4, "3"}, // KEY_3
        {5, "4"}, // KEY_4
        {6, "5"}, // KEY_5
        {7, "6"}, // KEY_6
        {8, "7"}, // KEY_7
        {9, "8"}, // KEY_8
        {10, "9"}, // KEY_9
        {59, "F1"}, // KEY_F1
        {60, "F2"}, // KEY_F2
        {61, "F3"}, // KEY_F3
        {62, "F4"}, // KEY_F4
        {63, "F5"}, // KEY_F5
        {64, "F6"}, // KEY_F6
        {65, "F7"}, // KEY_F7
        {66, "F8"}, // KEY_F8
        {67, "F9"}, // KEY_F9
        {68, "F10"}, // KEY_F10
        {87, "F11"}, // KEY_F11
        {88, "F12"}, // KEY_F12
        {183, "F13"}, // KEY_F13
        {184, "F14"}, // KEY_F14
        {185, "F15"}, // KEY_F15
        {186, "F16"}, // KEY_F16
        {187, "F17"}, // KEY_F17
        {188, "F18"}, // KEY_F18
        {189, "F19"}, // KEY_F19
        {190, "F20"}, // KEY_F20
        {191, "F21"}, // KEY_F21
        {192, "F22"}, // KEY_F22
        {193, "F23"}, // KEY_F23
        {194, "F24"}, // KEY_F24
        {30, "A"}, // KEY_A
        {48, "B"}, // KEY_B
        {46, "C"}, // KEY_C
        {32, "D"}, // KEY_D
        {18, "E"}, // KEY_E
        {33, "F"}, // KEY_F
        {34, "G"}, // KEY_G
        {35, "H"}, // KEY_H
        {23, "I"}, // KEY_I
        {36, "J"}, // KEY_J
        {37, "K"}, // KEY_K
        {38, "L"}, // KEY_L
        {50, "M"}, // KEY_M
        {49, "N"}, // KEY_N
        {24, "O"}, // KEY_O
        {25, "P"}, // KEY_P
        {16, "Q"}, // KEY_Q
        {19, "R"}, // KEY_R
        {31, "S"}, // KEY_S
        {20, "T"}, // KEY_T
        {22, "U"}, // KEY_U
        {47, "V"}, // KEY_V
        {17, "W"}, // KEY_W
        {45, "X"}, // KEY_X
        {21, "Y"}, // KEY_Y
        {44, "Z"}, // KEY_Z
    };
    if (platform == Input::KeyPlatform::Windows) {
        if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, static_cast<char>(key));
        if (key >= 0x70 && key <= 0x87) return "F" + std::to_string(key - 0x70 + 1);
        if (key >= 0x60 && key <= 0x69) return "Numpad " + std::to_string(key - 0x60);
        for (const auto& entry : windowsKeys) if (key == entry.code) return tr(entry.name);
    } else {
        for (const auto& entry : evdevKeys) if (key == entry.code) return tr(entry.name);
    }
    return tr("Key ") + std::to_string(key);
}
std::string key_label(int key) {
#ifdef _WIN32
    // OEM punctuation depends on the active keyboard layout. Ask Windows for
    // its key name rather than assuming the US ANSI punctuation characters.
    const bool oem = (key >= VK_OEM_1 && key <= VK_OEM_3) ||
        (key >= VK_OEM_4 && key <= VK_OEM_8) || key == VK_OEM_102;
    if (oem) {
        const UINT scan = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VK_TO_VSC_EX);
        const LONG flags = static_cast<LONG>(((scan & 0xff) << 16) | ((scan & 0xff00) ? 1 << 24 : 0));
        wchar_t name[128]{};
        if (scan && GetKeyNameTextW(flags, name, 128)) {
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, name, -1, nullptr, 0, nullptr, nullptr);
            if (bytes > 1) {
                std::string result(bytes, '\0');
                WideCharToMultiByte(CP_UTF8, 0, name, -1, result.data(), bytes, nullptr, nullptr);
                result.pop_back();
                return result;
            }
        }
    }
#endif
    return key_label(key, Input::kNativeKeyPlatform);
}
std::string binding_label(const UserConfig::Settings& settings, std::size_t control) {
    std::string result;
    for (const int key : UserConfig::control_keys(settings, control)) {
        if (!result.empty()) result += " / ";
        result += key_label(key);
    }
    return "[" + (result.empty() ? tr("Unbound") : result) + "]";
}
void set_language(Language language) { activeLanguage = language; }
Language language() { return activeLanguage; }
void initialize_language(int savedLanguage) {
    activeLanguage = savedLanguage == 1 ? Language::English : Language::Chinese;
    const char* value = std::getenv("TETR_UI_LANGUAGE");
    if (!value) return;
    const std::string_view name(value);
    if (name == "en" || name == "en-US") activeLanguage = Language::English;
    else if (name == "zh" || name == "zh-CN") activeLanguage = Language::Chinese;
}
std::string tr(std::string_view english) {
    if (activeLanguage == Language::Chinese)
        for (const auto& entry : dictionary) if (entry.first == english) return std::string(entry.second);
    return std::string(english);
}
std::string breadcrumb(std::string_view path) {
    std::string result;
    while (!path.empty()) {
        const auto end = path.find(" > ");
        result += tr(path.substr(0, end));
        if (end == std::string_view::npos) break;
        result += " > "; path.remove_prefix(end + 3);
    }
    return result;
}
std::string connection_status(std::string_view status) {
    for (const auto prefix : {"Connecting to ", "Connected to "}) {
        const std::string_view start(prefix);
        if (status.substr(0, start.size()) == start) return tr(start) + std::string(status.substr(start.size()));
    }
    return tr(status);
}
std::vector<std::string> breadcrumb_rows(std::string_view path, std::size_t columns) {
    std::vector<std::string> rows(1);
    bool first = true;
    while (!path.empty()) {
        const auto end = path.find(" > ");
        const auto part = (first ? std::string{} : " > ") + tr(path.substr(0, end));
        if (!rows.back().empty() && Util::TerminalText::width(rows.back() + part) > columns)
            rows.emplace_back();
        const auto wrapped = Util::TerminalText::wrap(part, columns);
        rows.back() += wrapped.front();
        rows.insert(rows.end(), wrapped.begin() + 1, wrapped.end());
        if (end == std::string_view::npos) break;
        path.remove_prefix(end + 3); first = false;
    }
    return rows;
}
}
