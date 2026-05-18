import argparse
import asyncio
import json
import struct
import time
from dataclasses import dataclass, field
from typing import Dict, Tuple

MAGIC = 0x00114514

TYPE = {
    "LOGIN": 1,
    "LOGOUT": 2,
    "REGISTER": 3,
    "ACK": 4,
    "CHAT": 5,
    "PRIVATE_CHAT": 6,
    "USER_LIST": 7,
    "ERROR": 8,
    "GROUP_CHAT_HISTORY": 9,
    "PRIVATE_CHAT_HISTORY": 10,
}


@dataclass
class Stats:
    sent: int = 0
    ok: int = 0
    fail: int = 0
    latencies_ms: list = field(default_factory=list)

    def record(self, ok: bool, latency_ms: float) -> None:
        self.sent += 1
        if ok:
            self.ok += 1
        else:
            self.fail += 1
        self.latencies_ms.append(latency_ms)

    def summary(self, duration_s: float) -> Dict[str, float]:
        avg = sum(self.latencies_ms) / len(self.latencies_ms) if self.latencies_ms else 0.0
        p95 = 0.0
        if self.latencies_ms:
            data = sorted(self.latencies_ms)
            idx = int(len(data) * 0.95) - 1
            idx = max(0, min(idx, len(data) - 1))
            p95 = data[idx]
        qps = self.sent / duration_s if duration_s > 0 else 0.0
        return {
            "sent": float(self.sent),
            "ok": float(self.ok),
            "fail": float(self.fail),
            "qps": qps,
            "avg_ms": avg,
            "p95_ms": p95,
        }


def pack_frame(msg_type: int, body: Dict) -> bytes:
    body_bytes = json.dumps(body, ensure_ascii=False).encode("utf-8")
    header = struct.pack("!I H I", MAGIC, msg_type, len(body_bytes))
    return header + body_bytes


async def read_exact(reader: asyncio.StreamReader, n: int) -> bytes:
    chunks = []
    remaining = n
    while remaining > 0:
        data = await reader.read(remaining)
        if not data:
            raise ConnectionError("connection closed while reading")
        chunks.append(data)
        remaining -= len(data)
    return b"".join(chunks)


async def send_frame(writer: asyncio.StreamWriter, msg_type: int, body: Dict) -> None:
    writer.write(pack_frame(msg_type, body))
    await writer.drain()


async def recv_frame(reader: asyncio.StreamReader) -> Tuple[int, Dict]:
    header = await read_exact(reader, 10)
    magic, msg_type, length = struct.unpack("!I H I", header)
    if magic != MAGIC:
        raise ValueError(f"bad magic: {hex(magic)}")
    body = {}
    if length > 0:
        body_bytes = await read_exact(reader, length)
        body = json.loads(body_bytes.decode("utf-8"))
    return msg_type, body


async def register_user(host: str, port: int, username: int, password: str, display_name: str) -> None:
    reader, writer = await asyncio.open_connection(host, port)
    try:
        body = {
            "username": username,
            "password": password,
            "display_name": display_name,
        }
        await send_frame(writer, TYPE["REGISTER"], body)
        await recv_frame(reader)
    finally:
        writer.close()
        await writer.wait_closed()


async def login(host: str, port: int, username: int, password: str) -> str:
    reader, writer = await asyncio.open_connection(host, port)
    try:
        body = {"username": username, "password": password}
        await send_frame(writer, TYPE["LOGIN"], body)
        _, resp = await recv_frame(reader)
        token = resp.get("token", "") if isinstance(resp, dict) else ""
        return token
    finally:
        writer.close()
        await writer.wait_closed()


async def chat_send_once(host: str, port: int, username: int, token: str) -> bool:
    reader, writer = await asyncio.open_connection(host, port)
    try:
        msg_body = {
            "username": username,
            "msg": "hello",
            "token": token,
        }
        await send_frame(writer, TYPE["CHAT"], msg_body)
        _, resp = await recv_frame(reader)
        return isinstance(resp, dict) and "msg" in resp
    finally:
        writer.close()
        await writer.wait_closed()


async def chat_worker(
    host: str,
    port: int,
    username: int,
    password: str,
    token: str,
    duration_s: float,
    rate_per_conn: float,
    stats: Stats,
) -> None:
    if not token:
        token = await login(host, port, username, password)
    if rate_per_conn <= 0:
        return
    interval = 1.0 / rate_per_conn
    start = time.perf_counter()
    next_send = start
    while True:
        now = time.perf_counter()
        if now - start >= duration_s:
            break
        if now < next_send:
            await asyncio.sleep(next_send - now)
        t0 = time.perf_counter()
        try:
            ok = await chat_send_once(host, port, username, token)
        except Exception:
            ok = False
        t1 = time.perf_counter()
        stats.record(ok, (t1 - t0) * 1000.0)
        next_send += interval


async def login_qps_worker(
    host: str,
    port: int,
    username: int,
    password: str,
    duration_s: float,
    rate_per_conn: float,
    stats: Stats,
) -> None:
    if rate_per_conn <= 0:
        return
    interval = 1.0 / rate_per_conn
    start = time.perf_counter()
    next_send = start
    while True:
        now = time.perf_counter()
        if now - start >= duration_s:
            break
        if now < next_send:
            await asyncio.sleep(next_send - now)
        reader, writer = await asyncio.open_connection(host, port)
        t0 = time.perf_counter()
        ok = False
        try:
            body = {"username": username, "password": password}
            await send_frame(writer, TYPE["LOGIN"], body)
            _, resp = await recv_frame(reader)
            ok = isinstance(resp, dict) and "msg" in resp
        except Exception:
            ok = False
        finally:
            writer.close()
            await writer.wait_closed()
        t1 = time.perf_counter()
        stats.record(ok, (t1 - t0) * 1000.0)
        next_send += interval


def parse_duration(value: str) -> float:
    value = value.strip().lower()
    if value.endswith("ms"):
        return float(value[:-2]) / 1000.0
    if value.endswith("s"):
        return float(value[:-1])
    if value.endswith("m"):
        return float(value[:-1]) * 60.0
    return float(value)


async def run_chat_test(args) -> None:
    duration_s = parse_duration(args.duration)
    rate = float(args.rate)
    if args.rate_mode == "total":
        rate_per_conn = rate / args.concurrency
    else:
        rate_per_conn = rate

    stats = Stats()
    tasks = [
        asyncio.create_task(
            chat_worker(
                args.host,
                args.port,
                args.username,
                args.password,
                args.token,
                duration_s,
                rate_per_conn,
                stats,
            )
        )
        for _ in range(args.concurrency)
    ]
    start = time.perf_counter()
    await asyncio.gather(*tasks)
    elapsed = time.perf_counter() - start
    print("CHAT summary:", stats.summary(elapsed))


async def run_login_test(args, username: int, password: str, label: str) -> None:
    duration_s = parse_duration(args.duration)
    rate = float(args.rate)
    if args.rate_mode == "total":
        rate_per_conn = rate / args.concurrency
    else:
        rate_per_conn = rate

    stats = Stats()
    tasks = [
        asyncio.create_task(
            login_qps_worker(
                args.host,
                args.port,
                username,
                password,
                duration_s,
                rate_per_conn,
                stats,
            )
        )
        for _ in range(args.concurrency)
    ]
    start = time.perf_counter()
    await asyncio.gather(*tasks)
    elapsed = time.perf_counter() - start
    print(f"{label} summary:", stats.summary(elapsed))


def main() -> None:
    parser = argparse.ArgumentParser(description="TCP chat protocol stress test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1316)
    parser.add_argument("--username", type=int, default=10000001)
    parser.add_argument("--password", default="5365220")
    parser.add_argument("--display-name", default="test")
    parser.add_argument("--concurrency", type=int, default=4)
    parser.add_argument("--duration", default="60s")
    parser.add_argument("--rate", default="100")
    parser.add_argument("--rate-mode", choices=["total", "per-conn"], default="total")
    parser.add_argument("--token", default="")
    parser.add_argument(
        "--mode",
        choices=["register", "login-db", "login-nodb", "chat", "all"],
        default="all",
    )
    args = parser.parse_args()

    async def runner() -> None:
        if args.mode in ("register", "all"):
            try:
                await register_user(args.host, args.port, args.username, args.password, args.display_name)
                print("REGISTER: ok")
            except Exception as exc:
                print(f"REGISTER: failed ({exc})")

        if args.mode in ("login-db", "all"):
            await run_login_test(args, args.username, args.password, "LOGIN-DB")

        if args.mode in ("login-nodb", "all"):
            bad_user = 99999999
            bad_pass = "badpass"
            await run_login_test(args, bad_user, bad_pass, "LOGIN-NODB")

        if args.mode in ("chat", "all"):
            await run_chat_test(args)

    asyncio.run(runner())


if __name__ == "__main__":
    main()
