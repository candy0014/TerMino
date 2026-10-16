#!/usr/bin/env python3
"""Save a Cloudflare Tunnel token privately on the connector computer."""
import base64
import getpass
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import uuid


def parse_token(value):
    # Accept either the token itself or the single install-command line that
    # contains it. Pasted text is data and is never executed.
    match = re.search(r"(?<![A-Za-z0-9+/])eyJ[A-Za-z0-9+/=]+", value)
    if not match or len(match.group()) > 8192:
        raise ValueError("missing token")
    token = match.group()
    data = json.loads(base64.b64decode(token, validate=True))
    if not isinstance(data, dict) or not isinstance(data.get("a"), str) or not data["a"]:
        raise ValueError("missing account")
    if not isinstance(data.get("s"), str) or not base64.b64decode(data["s"], validate=True):
        raise ValueError("missing secret")
    tunnel_id = str(uuid.UUID(data["t"]))
    return token, tunnel_id


def main():
    if getpass.getuser() != "tetu":
        print("请在机房电脑以 tetu 用户运行此程序。")
        return 1
    if not sys.stdin.isatty():
        print("请直接在机房电脑的交互式终端运行，令牌会隐藏输入。")
        return 1
    print("请复制 Cloudflare 安装命令末尾以 eyJ 开头的令牌。")
    print("也可粘贴包含令牌的那一行安装命令，程序只提取令牌。")
    print("输入不会显示；粘贴后按回车，Ctrl+C 取消。")
    while True:
        value = getpass.getpass("粘贴隧道令牌: ")
        if not value.strip():
            print("输入为空，尚未保存。请重新粘贴令牌并按回车。")
            continue
        try:
            token, tunnel_id = parse_token(value)
        except (ValueError, KeyError, TypeError):
            print("令牌格式不正确，尚未保存。请重新复制完整令牌。")
            continue
        break
    config = Path.home() / ".config" / "termino-cloudflare"
    config.mkdir(parents=True, exist_ok=True, mode=0o700)
    config.chmod(0o700)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="ascii", dir=config,
                                         prefix="token-", delete=False) as file:
            temporary = Path(file.name)
            os.fchmod(file.fileno(), 0o600)
            file.write(token)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary, config / "token")
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    print("令牌格式验证通过，已保存。隧道 ID:", tunnel_id)
    result = subprocess.run(["systemctl", "--user", "restart", "termino-cloudflare.service"],
                            capture_output=True, text=True)
    if result.returncode:
        print("令牌已保存，但启动隧道失败。请回复此提示，我会继续检查。")
        return 1
    print("隧道已启动。请回复“验证保存成功”，我会检查公网连接。")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyboardInterrupt, EOFError):
        print("\n已取消。")
        sys.exit(1)
