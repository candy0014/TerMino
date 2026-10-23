# 2025／2026 活动块出生几何核对

本次核对使用活动期间的官方客户端。当前 `https://tetr.io/js/tetrio.js`
已经移除了这些活动定义，因此不能用当前普通方块库代替活动版本。

| 活动 | 官方资源的存档 | SHA256 |
| --- | --- | --- |
| 2025 PENTR.IO／A Fool’s Errand | [2025-04-02 官方客户端](https://web.archive.org/web/20250402002227id_/https://tetr.io/js/tetrio.js?hv=311c3a7ad.5477aa6f428e.036aa2f9cb6a97) | `6c06cfecae9e7d072cbd75bb2b85bf652a463c25d5163c55b89fde312fa1eaaf` |
| 2026 Mutation | [2026-04-07 官方客户端](https://web.archive.org/web/20260407091103id_/https://tetr.io/js/tetrio.js) | `a910b10f51e8780e0b4856ab21c2fd5d3cb71d151e173f220d6ea8f7cc1efa5c` |

## 官方坐标语义

官方 `Next` 使用 `GetPieceSpawnRotation` 和 `GetPieceOffset`，生成浮点原点
`x = ceil(width / 2) - 1 + offsetX`、`y = buffer - 2.04 + offsetY`。
`GetPieceDefinition` 返回矩阵的 `dx`／`dy`；绘制和碰撞将每格换算为
`x + rawX - dx`、`ceil(y) + rawY - dy`。

因此，以可见版面顶端为第 0 行时，初始格子是：

```text
row = -2 + offsetY + rawY - dy
col = (width - 1) / 2 + offsetX + rawX - dx  // 整数除法
```

2025 的 ASCDX 和 2026 的 SRS+ 对这些活动块没有额外出生偏移或初始旋转。
需要保留每块真实的矩阵原点，不能把所有块底部对齐到相同一行。

## 修正范围与兼容

- 18 种五连块：修正 V、T5、X、Z5、S5 等的矩阵原点，W／F／K 的初始朝向，
  B／D 名称对应的镜像，以及 J5／L5／Y／R 等旋转态的原点。
- 60 种六连块：52 种的矩阵原点存在差异，多数原来向上偏一行；
  alnitak／alioth 各旋转态的偏移分别核对。
- 70 种 2026 特殊块：O8 的四个旋转态向右修正一格，其余 69 种确认一致。
- 保留原有 0～228 类型的几何与标识，追加 229～376 作为活动的正确矩阵。
  两个前端、出生标记、Hold 后重新出生、碰撞和 Bot 都消费同一共享目录。
- 新配置写入 `EventGeometryVersion=1`；缺少此字段的录像仍选择旧活动定义。
  已有地图文件不改写；新对局在内存中映射活动块到正确定义，旧录像保留旧序列。
- 预览另做原点归一化，避免碰撞矩阵修正导致终端 Hold／Next 偏移。

## 可重复验证

`tools/audit_tetrio_event_geometry.cjs` 对输入文件做上述 SHA256 校验，提取并离线执行
原始 `Next`、`GetPieceOffset`、`GetPieceSpawnRotation`、`GetPieceDefinition`。
测试桩只提供空场、关闭 IRS／IHS／20G 的运行环境，不重写出生方法；
没有登录、联网、界面或对战代码执行。

```sh
node tools/audit_tetrio_event_geometry.cjs \
  /path/to/tetrio-20250402.js /path/to/tetrio-20260407.js ./build/tests/tetr_core_tests piece_catalog_test
```

共核对 148 种活动块的 592 个旋转矩阵、宽度 4／9／10／11／20／100 下的
888 个出生原点及 1776 组有序踢墙测试。原始客户端仅保存在本地核对资料中，
不进入游戏资源或发行包。

## 本轮验证结果

- Linux 使用 `./compile.sh --frontend both` 完成两端构建。
- 官方几何核对全部通过；30,000 组块序对照同时覆盖新旧几何版本，
  队列与随机数消耗一致；另行验证 10,000 组网络棋盘编解码，当前／Hold／Next
  从全部 377 个类型中取样，覆盖超过 255 的新标识。
- 169 项完整 CTest 已运行。首轮 166 项通过；补齐块序与统计回归对新增标识的
  兼容检查后，这两项各自通过，合计 168 项通过。
- 剩余 `board_view_steady_8` 未通过：1.1 秒的 30Hz 联网采样中某来源收到 26 次，
  测试要求至少 27 次，单独复跑仍复现。此前
  `goal-preview-layout-20261018/network-retry.log` 也记录过相同 26/27 阈值失败；
  本轮没有修改该测试或网络调度实现，不将全量结果记录为 169/169。
- 隐藏离屏 OpenGL 的 10×20、4×4、10×8 画面核对通过。
  3 张临时 BMP 已删除，只保留 10×20 与 4×4 两张关键裁剪 PNG。
- 架构边界和已跟踪公开源码审计通过；Windows 原生图形／输入未在本轮验证，
  验收状态继续保留 pending；没有打包或发布，也没有改写本地个人 `.tetr`。

完整日志、原始参考、裁剪图与可逆补丁位于
`build-migration-backups/spawn-headroom-20261018/`。
