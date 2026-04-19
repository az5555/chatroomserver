# 网络聊天室自定义协议文档（修订版）

## 0. 目标
本文件定义客户端与服务器之间基于 TCP 的二进制帧协议：固定包头（binary header）+ JSON 包体（UTF-8）。重点说明头部结构、数字枚举映射、字节序、最大长度、以及常见错误处理建议，便于实现互操作性与测试。

## 1. 协议概述
本协议采用“包头（binary header）+ JSON 包体（UTF-8）”的格式，解决 TCP 粘包/拆包问题。包头为固定 10 字节：魔数（4B）+ 类型（2B）+ body 长度（4B）。所有头字段使用网络字节序（big-endian）。

重要设计原则：
- 头部决定包体长度，接收端应先完整读取头部再按长度读取 body。
- 头部的 `type` 为二进制枚举（uint16），用于快速分发；JSON 包体内不强制要求 `type` 字段（可选冗余）。

## 2. 帧结构（Frame）

总体格式：

```plain
[Header (10 bytes)] + [Body (JSON, len bytes, UTF-8)]
```

### 2.1 Header（10 字节）

- 魔数 Magic (4 bytes, uint32, big-endian)：用于快速检测帧边界与协议一致性。
- 类型 Type (2 bytes, uint16, big-endian)：消息类型枚举（见第 3 节）。
- Body 长度 Len (4 bytes, uint32, big-endian)：紧随其后的 JSON body 的字节数。

推荐魔数（示例，请在实现中确认值并保持一致）：

- Magic = 0x00114514

注意：实现可以先读 10 字节完整头部，或先读 4 字节魔数做快速验证，再读剩余 6 字节。任意一种方式都必须保证一致性与可靠的错误处理。

### 2.2 Body（JSON）

- Body 为 UTF-8 编码的 JSON 文本，长度由 Header.Len 指定。
- Body 字段用于传输具体的请求/响应数据，字段命名请参照第 4 节。

### 2.3 最大长度与防护

- 建议定义最大 body 长度常量 `MAX_BODY_LEN`（例如 4096 字节，视需求调整）。
- 若 Header.Len 为 0，表示空 body（合法）。若 Header.Len > MAX_BODY_LEN，应当：
  - 拒绝该帧并关闭连接，或
  - 返回 `ERROR` 帧（见第 4.7）后再断开，或
  - 按实现策略记录并忽略后续流量，但务必避免 OOM。

## 3. 二进制 `type` 枚举（Header.Type）

Header.Type 为 uint16 枚举，必须在实现中与代码保持一致。示例映射：

| 值 (uint16) | 名称 | 说明 |
|---:|:---|:---|
| 1 | LOGIN | 登录请求 |
| 2 | ACK | 通用应答/结果 |
| 3 | CHAT | 公聊消息 |
| 4 | PRIVATE_CHAT | 私聊消息 |
| 5 | USER_LIST | 用户列表 |
| 6 | LOGOUT | 退出请求 |
| 7 | ERROR | 错误通知 |
| 8 | REGISTER | 注册请求 |
| 9 | GROUP_CHAT_HISTORY | 群聊历史 |
| 10 | PRIVATE_CHAT_HISTORY | 私聊历史 |

说明：JSON 包体内可以包含同语义的字符串字段（例如 "type": "LOGIN"）作为可读冗余，但服务端分发应以 Header.Type 为准。

## 4. JSON Body 字段约定（按消息类型）

通用说明：
- 为避免歧义，建议区分 `user_id`（整型、唯一标识符）与 `display_name` / `username`（字符串、展示用途）。如果系统以数字作为用户名（例如数据库自增 id），请在协议中统一使用 `user_id`（整型）。
- 所有示例 JSON 均为 UTF-8 编码。
- 通用 ACK 模板：

```json
{
  "status": "ok" | "error",
  "msg": "人类可读的说明",
  "data": { ... } // 可选
}
```

### 4.1 LOGIN（Header.Type = 1）

客户端 -> 服务器：

```json
{
  "user_id": 114,        // 推荐使用数值 id，或使用 "username": "alice"（字符串）但请保持一致
  "password": "123456"
}
```

服务器应返回 ACK（Header.Type = 2）：

```json
{
  "status": "ok",
  "msg": "login successful",
  "data": { "token": "<session_token>", "user_id": 114 }
}
```

### 4.2 REGISTER（Header.Type = 8）

客户端 -> 服务器：

```json
{
  "username": "alice",     // 显示名
  "password": "123456",
  "display_name": "Alice Zhang" // 可选
}
```

服务器 ACK（示例）：

```json
{
  "status": "ok",
  "msg": "registration successful",
  "data": { "user_id": 114 }
}
```

### 4.3 CHAT（Header.Type = 3）

客户端发送公聊：

```json
{
  "user_id": 114,
  "msg": "大家好！",
  "token": "<session_token>"
}
```

服务器广播给其他客户端的通知（示例）：

```json
{
  "user_id": 114,
  "display_name": "Alice Zhang",
  "msg": "大家好！",
  "time": "2026-04-19T12:34:56Z"
}
```

### 4.4 PRIVATE_CHAT（Header.Type = 4）

客户端 -> 服务器：

```json
{
  "from_id": 114,
  "to_id": 115,
  "msg": "私下说个事",
  "token": "<session_token>"
}
```

服务器转发给目标客户端的通知：

```json
{
  "from_id": 114,
  "msg": "私下说个事",
  "time": "2026-04-19T12:35:00Z"
}
```

### 4.5 USER_LIST（Header.Type = 5）

服务器响应或主动推送（示例）：

```json
{
  "status": "ok",
  "data": [ { "user_id": 114, "display_name": "Alice" }, { "user_id":115, "display_name":"Bob" } ]
}
```

### 4.6 LOGOUT（Header.Type = 6）

客户端请求：

```json
{ "user_id": 114, "token": "<session_token>" }
```

服务器 ACK：

```json
{ "status": "ok", "msg": "logout successful" }
```

### 4.7 ERROR（Header.Type = 7）

服务器在出现可预期错误时返回：

```json
{
  "status": "error",
  "code": 403,
  "msg": "用户名不存在"
}
```

建议定义常用错误码表（示例）：

- 400: Bad Request / 解析错误
- 401: Unauthorized / token 无效
- 403: Forbidden / 权限不足
- 404: Not Found / 目标用户或资源不存在
- 413: Payload Too Large / 超大帧长度

### 4.8 群聊/私聊历史（Header.Type = 9 / 10）

请求示例：

```json
{ "token": "<session_token>", "offset": 0, "limit": 50 }
```

返回示例：

```json
{
  "status": "ok",
  "messages": [ { "user_id":114, "msg":"...", "time":"2026-04-10T15:30:00Z", "display_name":"Alice" } ],
  "next_offset": 50
}
```

## 5. 接收端参考伪码

```pseudo
function recv_frame(socket):
  header = read_exact(socket, 10)  // 阻塞或循环读取直到 10 字节
  magic = ntohl(header[0:4])
  if magic != MAGIC: error
  type = ntohs(header[4:6])
  len = ntohl(header[6:10])
  if len > MAX_BODY_LEN: error_or_close()
  body = read_exact(socket, len)
  json = parse_json(body)
  dispatch(type, json)
```

实现细节：
- 对于非阻塞 socket，read_exact 应在循环中累积，处理 EAGAIN/EWOULDBLOCK。
- 对于 ET(epoll edge-trigger) 模式，必须在读取循环中读尽数据。

## 6. 调试与示例辅助

- 建议在调试阶段提供“示例帧（hex）”以便 tcpdump/wireshark 辅助定位。
- 建议在协议文档旁附带一个小型测试程序或 Python 脚本，用于构造合法/非法帧并与服务器交互。

## 7. 兼容性注意事项

- 明确 `user_id` 类型（int）或 `username`（string）並在全部示例中保持一致。
- 明确 Header.Type 的数值映射并在代码里维护同一份枚举定义（建议单文件/头文件导出枚举）。

---

（文档修订结束）
