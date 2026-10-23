# TerMino 隧道自动恢复

`deploy/tunnel_watchdog.py` 使用 cloudflared 本机 `/ready` 判断是否至少有一条
有效的边缘连接，同时检查 UDP 中继 `!probe` 与 WebSocket 网关 `/healthz`。
探测使用回环地址并忽略代理环境，不依赖公网 DNS 或用户桌面代理端口。
Cloudflare 对就绪接口的定义见
[官方部署文档](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/deployment-guides/kubernetes/)。

`termino-tunnel-watchdog.timer` 每次检查结束 15 秒后再检查。只有连续三次失败、
本地两个源服务健康且隧道已运行至少 60 秒时才重启隧道。首次重试间隔至少 120 秒，
持续失败时增加到 240、300 秒；使用文件锁和先落盘的冷却状态防止重启风暴。
重启或过时的采样不会继承连续失败次数，主动停止的服务也不会被强行拉起。
剩余一条有效连接时保持服务，避免为补齐连接数中断现有连接。

三个网络服务的 `recovery.conf` 将进程退出后的策略设为 `Restart=always`、等待 5 秒，
手工 `systemctl --user stop` 仍然有效。安装不修改原有 route.conf、VPN 路由、DNS、
隧道 token、中继二进制和网关部署，也不重启正在正常运行的正式服务。

将以下文件放到服务器同一目录，再以原部署用户执行安装：

```sh
python3 install_tunnel_watchdog.py
```

需要的文件为 `install_tunnel_watchdog.py`、`tunnel_watchdog.py`、
`termino-tunnel-watchdog.service`、`termino-tunnel-watchdog.timer` 和
`termino-network-recovery.conf`。安装前会核对三个既有服务与 token 文件是否存在，
不会读取 token 内容。覆盖文件及原定时器状态备份到
`~/termino-cloudflare/watchdog-backups/<时间戳>/`，安装失败会自动还原。

```sh
python3 ~/termino-cloudflare/tunnel_watchdog.py --check
systemctl --user status termino-tunnel-watchdog.timer
journalctl --user -u termino-tunnel-watchdog.service -n 30 --no-pager
python3 install_tunnel_watchdog.py --rollback ~/termino-cloudflare/watchdog-backups/<时间戳>
```

这是服务与隧道恢复机制。隧道重新建立后可接受新连接；已经中断的 WebSocket 会话
和对局是否能恢复仍取决于客户端与中继协议，此部署没有增加玩家会话恢复协议。

## 2026-10-18 部署验证

原故障的证据是 cloudflared 进程仍存在，但不断出现 QUIC 无网络活动超时和无可用
边缘地址，公网返回 1033。本地中继和网关一直正常。旧进程反复重试缓存的 FakeIP
地址；重新启动并解析端点后恢复两条有效连接。日志支持“进程存活但隧道断开”，
现有证据不足以确定最初是哪一次 VPN、网络或映射变化触发。

已在旧电脑安装并启用定时器，用户 linger 已启用。使用独立本地就绪服务与独立
systemd 定时器模拟断线，按正式 15 秒间隔采样，第 3 次检查自动重启并恢复就绪；
三个正式服务的 PID 前后相同。完成后已停止并清理独立测试服务。
公网 `/healthz` 返回 200，真实 WebSocket 握手与 UDP 中继往返成功。

13 项 Python 测试覆盖暂时故障、启动宽限、进程变化、重启/过时状态、手工停止、
源站失败门控、冷却退避、重启失败前持久化、损坏状态、代理绕过、安装回滚与
不重启正式服务。Linux CTest 注册了 `tunnel_watchdog`。
证据保存在 `build-migration-backups/tunnel-recovery-20261018/`。
