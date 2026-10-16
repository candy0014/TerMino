# 联机低延迟改造（2026-10-15）

已把 TETR.IO 调研中适合本项目的机制落实到客户端和网关：数据就绪即收发、独立远端时间线、按抖动调整播放缓冲、积压时追赶，以及完整/缩略画面继续按需订阅。本地动作先执行，攻击与回合控制保持独立可靠处理。

本轮没有移植官方客户端代码，也没有将可自定义规则的引擎替换为 TETR.IO 输入回放引擎。继续使用本项目带帧号的动作结果与校准快照，以保留不同地图、个人手感、重力和旋转规则的兼容性。

## 收发路径

- 删除客户端和 Python 网关的固定 8ms 视觉合并等待。队列有数据就发送已有批次；同批入队或背压时，仍可覆盖相同玩家、版本和方块的尚未发送位姿。
- 攻击、校准快照、回合控制与未知扩展仍是顺序屏障；视觉合并不能跨过这些消息。
- WebSocket 工作线程改用真实套接字就绪等待。网络到包唤醒读处理，发送入队与关闭通过 `curl_multi_wakeup` 唤醒工作线程，不再每 2/10ms 采样网络。
- 仅在存在未完成发送时等待可写，避免空闲可写套接字造成忙循环。一次收到超过 64 段时，先继续清空 curl/TLS 内部缓冲，再等待 OS 套接字，避免内部已有数据却进入一秒空闲等待。
- 显式保留 TCP_NODELAY、TLS 证书校验、2MiB 有界队列、64KiB/64 消息批次上限，以及游戏线程的非阻塞查询。

就绪与跨线程唤醒用法依据 [curl_multi_poll](https://curl.se/libcurl/c/curl_multi_poll.html) 和 [curl_multi_wakeup](https://curl.se/libcurl/c/curl_multi_wakeup.html)。

## 远端播放

之前每名玩家固定等待 35ms。现在以发送方 120Hz 帧时间戳维护独立时间线：

| 条件 | 行为 |
|---|---|
| 稳定链路 | 缓冲一帧，约 8.33ms |
| 到达时间抖动 | 快速增大缓冲，最多 50ms |
| 链路恢复稳定 | 按每秒 10ms 逐步回落至一帧 |
| 网络停顿后旧包成批到达 | 不将旧包的时间原点重新排到未来 |
| 位姿积压超过 50ms | 合并同版本、同方块、已经到期的连续旧位姿，保留较新的绝对结果 |
| 遇到快照/版本变化 | 保持顺序边界，不跨越它们追赶 |
| 64 条时间线队列满 | 优先淘汰可替换位姿，保留已有快照；只剩快照仍超限时保留有界恢复机制 |
| 缺少校准基准 | 请求快照，避免在错误棋盘上猜测 |

早到的顺序样本改善本地时间映射；迟到或乱序样本不能通过反复重新锚定使旧状态再多等一段时间。旧对局与已应用序号仍被拒绝。

这些参数是本项目的实现选择，并非声称 TETR.IO 使用相同的 8.33/50ms 参数。播放缓冲只作用于对手画面，不用于延迟本地输入或可靠攻击。

## 前后对照

基准使用相同机器、同一测试代码及 120 个单独视觉消息；修改前后二进制与网关源码分别冻结，交替运行三轮。每次确认返回的二进制消息与发送内容完全一致。

测量路径为：真实 C++ 客户端 → WebSocket 网关 → UDP 回环 echo → 网关 → 客户端。

| 轮次 | 修改前中位数 | 修改后中位数 | 修改前 p95 | 修改后 p95 |
|---|---:|---:|---:|---:|
| 1 | 19.189ms | 0.370ms | 19.476ms | 0.603ms |
| 2 | 19.201ms | 0.360ms | 19.553ms | 0.494ms |
| 3 | 19.228ms | 0.452ms | 19.552ms | 0.514ms |

原始结果：[20261015_low_latency_transport.json](../runlogs/20261015_low_latency_transport.json)。工具：[benchmark_websocket_latency.py](../tools/benchmark_websocket_latency.py)。

这是**本机传输往返**，不包含公网路由、远端播放缓冲、终端绘制或物理键盘/屏幕延迟。不能把约 0.4ms 写成公网对战或完整按键到画面的延迟。

```sh
cmake -S . -B /tmp/termino-low-latency-20261015 -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build /tmp/termino-low-latency-20261015 --target network_websocket_test -j4
python tools/benchmark_websocket_latency.py /tmp/termino-low-latency-20261015/network_websocket_test
```

Python 使用已安装 `deploy/websocket-requirements.txt` 的环境；`--gateway-dir` 可选择被冻结的网关源码。不要拿新二进制配旧源码目录当成完整“修改后”的结果。

## 回归与运行验证

独立 Release 构建并通过 12 项相关 CTest：`hybrid_board_sync`、`multiplayer_frame_boundary`、`network_wait`、`network_rooms`、`multiplayer_rooms`、`adaptive_board_view`、`board_view_stagger`、`multiplayer_round_lifecycle`、`room_host_config`、`network_connection`、`network_websocket`、`multiplayer_countdown_websocket`。

关键覆盖包括一帧缓冲、抖动增大/衰减、乱序、旧局隔离、停顿后的追赶、保留唯一快照、追赶不跨快照，以及真实新旧 WebSocket/UDP 房间、攻击确认、TLS 拒绝、分片、Ping/Close、160 条可靠消息连续接收。带延迟的生命周期测试注入 80ms 下行及 750ms RoundReady 延迟。

- [Linux 回归日志](../runlogs/20261015_low_latency_regression.log)
- [Linux WebSocket 日志](../runlogs/20261015_low_latency_websocket.log)
- [Windows/Wine 时间线日志](../runlogs/20261015_low_latency_windows_hybrid.log)
- [Windows/Wine WebSocket 日志](../runlogs/20261015_low_latency_windows_websocket.log)
- [公网只读 probe](../runlogs/20261015_low_latency_public_probe.log)

Linux 的 `build/tetr` / `build/tetr_server` 和 Windows 的 `build-portable-windows-wine/tetr.exe` / `tetr_server.exe` 已重新编译。Windows 的时间线测试和真实 WebSocket 集成也已通过 Wine 运行；这不等同于原生 Windows 显示器的像素延迟测量。

另外用最终 Windows 基准程序通过真实 Win32 控制台输入队列注入按键，并核对 VT 输出中的方块位置。在线两组均使用可协商事件流的本地网关，额外注入 80ms 下行延迟：

| 情景 | 样本数 | 输入至 VT 输出中位数 | p95 | 最大值 |
|---|---:|---:|---:|---:|
| 离线 | 80 | 1.043ms | 1.287ms | 1.762ms |
| 双人、对手 120Hz 动作 | 80 | 1.006ms | 1.590ms | 1.931ms |
| 八人、对手 120Hz 动作并按需求缩略 | 60 | 1.022ms | 9.645ms | 46.125ms |

原始 CSV/JSON 分别为 [offline](../runlogs/windows_input_latency_20261015/low_latency_offline.json)、[motion120](../runlogs/windows_input_latency_20261015/low_latency_motion120.json)、[multi8](../runlogs/windows_input_latency_20261015/low_latency_multi8.json)。这些是 Wine 上的 `vt_stream` 观测，不包含物理键盘、终端渲染及显示器扫描；基准中的 model 时间戳仍是 canPlace 回调代理点，故这里只报告经输出校验的 VT 指标。

双人结果表明本次样本中，本地响应没有被额外的 80ms 下行延迟拖慢同等幅度；八人样本仍出现明显长尾。不能据本轮宣称所有人数、原生 Windows 或公网条件的本地手感延迟已完全解决。

## 兼容与部署边界

UDP 消息格式、`termino.events.v1` 外壳和旧网关回退保持兼容。使用旧网关时，新客户端的就绪唤醒和远端播放仍可生效；服务器应配套部署当前 `tools/websocket_gateway.py` 与 `tools/event_stream.py` 并重启网关，才能消除网关端原有的合并等待。

本轮更新源码与本机程序，并对现有公网地址执行只读探测；没有替换正在运行的公网服务、改变 DNS/隧道路由或发布安装包。公网绕路、TCP 重传及隧道拥塞仍需要独立测量，客户端排队优化不能消除它们。
