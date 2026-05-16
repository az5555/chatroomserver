import argparse
import asyncio
import json
import struct

MAGIC = 0x00114514

TYPE = {
    "LOGIN": 1,
    "ACK": 2,
    "CHAT": 3,
    "PRIVATE_CHAT": 4,
    "USER_LIST": 5,
    "LOGOUT": 6,
    "ERROR": 7,
    "REGISTER": 8,
    "GROUP_CHAT_HISTORY": 9,
    "PRIVATE_CHAT_HISTORY": 10,
}


def pack_frame(msg_type: int, body: dict) -> bytes:
    body_bytes = json.dumps(body, ensure_ascii=False).encode("utf-8")
    header = struct.pack("!I H I", MAGIC, msg_type, len(body_bytes))
    return header + body_bytes


async def read_exact(reader: asyncio.StreamReader, n: int) -> bytes:
    data = b""
    while len(data) < n:
        chunk = await reader.read(n - len(data))
        if not chunk:
            raise ConnectionError("connection closed while reading")
        data += chunk
    return data


async def send_frame(writer: asyncio.StreamWriter, msg_type: int, body: dict) -> None:
    writer.write(pack_frame(msg_type, body))
    await writer.drain()


async def recv_frame(reader: asyncio.StreamReader) -> dict:
    header = await read_exact(reader, 10)
    magic, msg_type, length = struct.unpack("!I H I", header)
    if magic != MAGIC:
        raise ValueError(f"bad magic: {hex(magic)}")
    body = {}
    if length > 0:
        body_bytes = await read_exact(reader, length)
        body = json.loads(body_bytes.decode("utf-8"))
    return {"type": msg_type, "body": body}


async def round_trip(host: str, port: int, msg_type: int, body: dict) -> dict:
    reader, writer = await asyncio.open_connection(host, port)
    try:
        await send_frame(writer, msg_type, body)
        return await recv_frame(reader)
    finally:
        writer.close()
        await writer.wait_closed()


async def main() -> None:
    parser = argparse.ArgumentParser(description="Simple protocol test")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1316)
    parser.add_argument("--username", type=int, default=10000001)
    parser.add_argument("--password", default="5365220")
    parser.add_argument("--display-name", default="test")
    args = parser.parse_args()

    print("REGISTER...")
    try:
        resp = await round_trip(
            args.host,
            args.port,
            TYPE["REGISTER"],
            {
                "username": args.username,
                "password": args.password,
                "display_name": args.display_name,
            },
        )
        print("REGISTER response:", resp)
    except Exception as exc:
        print("REGISTER failed:", exc)

    print("LOGIN...")
    token = ""
    try:
        resp = await round_trip(
            args.host,
            args.port,
            TYPE["LOGIN"],
            {"username": args.username, "password": args.password},
        )
        print("LOGIN response:", resp)
        token = resp.get("body", {}).get("token", "")
    except Exception as exc:
        print("LOGIN failed:", exc)

    print("CHAT...")
    try:
        resp = await round_trip(
            args.host,
            args.port,
            TYPE["CHAT"],
            {"username": args.username, "msg": "hello", "token": token},
        )
        print("CHAT response:", resp)
    except Exception as exc:
        print("CHAT failed:", exc)


if __name__ == "__main__":
    asyncio.run(main())
