#!/usr/bin/env python3
"""studio-bridge 假桥（imprint 仓 studio/ 冒烟与单测用）。

docs/09 §9.1 线协议的 stdlib-only 回声实现——**不需要天机仓在场**：
pty 冒烟与 spawn 单测用它端到端驱动客户端，不依赖真 Python 原型机。
行为：hello/ping/project.get 返回固定快照；console.input 的命令回
回执行、非命令（发言）回 turn_started 并在 0.2s 后主动下发
turn.finished 事件（无 id，唯一的 v1 事件）；/quit 回 quit 并退出。
"""

from __future__ import annotations

import json
import os
import sys
import time


def _mark(req: dict) -> None:
    """冒烟断言缝：把收到的请求行追加到 STUDIO_MOCK_SEEN 指定的文件——
    pty 冒烟由此断言「输入真的到达了桥」（帧数是弱信号，SIXEL 每次轮询
    重绘都会出帧）。"""
    path = os.environ.get("STUDIO_MOCK_SEEN", "")
    if not path:
        return
    try:
        with open(path, "a", encoding="utf-8") as f:
            f.write(json.dumps(req, ensure_ascii=False, sort_keys=True) + "\n")
    except OSError:
        pass


def respond(req: dict) -> dict | None:
    t = str(req.get("type", ""))
    if t == "bridge.hello":
        return {"bridge": "mock-bridge", "v": 1, "root": "/mock",
                "responder": "mock", "model": "",
                "seat_human": "human", "seat_ai": "ai.writer"}
    if t == "bridge.ping":
        return {"pong": True}
    if t == "project.get":
        return {"fp": "mock-1", "busy": False,
                "status": "会话 s_mock01 · 假桥 🔒",
                "subtitle": "作品 mock",
                "history": [{"id": "s_mock01", "title": "假桥会话",
                             "locked": True, "work": "mock", "current": True}],
                "dialogue": [
                    {"seat": "human", "kind": "human", "text": "你好",
                     "prompt_hash": ""},
                    {"seat": "ai.writer", "kind": "ai",
                     "text": "〔ai.writer·mock〕收到：「你好」。", "prompt_hash": "abc123"}],
                "text": "（假桥手稿：manuscript.md 的合成投影）"}
    if t == "console.input":
        text = str(req.get("text", ""))
        if text.startswith("/quit"):
            return {"quit": True}
        if text.startswith("/"):
            return {"lines": ["（假桥回执）" + text]}
        return {"lines": [], "turn_started": True}
    return None


def main() -> None:
    out = sys.stdout
    for line in iter(sys.stdin.readline, ""):
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
        except Exception:
            continue
        _mark(req)
        rid = str(req.get("id", ""))
        data = respond(req)
        if data is None:
            out.write(json.dumps(
                {"v": 1, "id": rid, "ok": False,
                 "error": {"code": "unknown_op",
                           "message": f"未知操作：{req.get('type')}"}},
                ensure_ascii=False) + "\n")
        else:
            out.write(json.dumps(
                {"v": 1, "id": rid, "ok": True, "data": data},
                ensure_ascii=False) + "\n")
        out.flush()
        t = str(req.get("type", ""))
        if t == "console.input" and data and data.get("turn_started"):
            time.sleep(0.2)  # 模拟 AI 回合时延；期间桥仍可应答其他请求
            out.write(json.dumps(
                {"v": 1, "type": "turn.finished",
                 "data": {"ok": True, "lines": [
                     "ai.writer（配方 abc123）：〔ai.writer·mock〕收到。"]}},
                ensure_ascii=False) + "\n")
            out.flush()
        if t == "console.input" and data and data.get("quit"):
            break


if __name__ == "__main__":
    main()
