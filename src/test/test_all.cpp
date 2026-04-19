#include <iostream>
#include <string>
#include <vector>
#include <cassert>

#include "../buffer/buffer.h"
#include "../protocol/protocolresponse.h"
#include "../protocol/protocolrequest.h"
#include "../protocol/json.hpp"
#include "../protocol/utils.h"

#include <arpa/inet.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <thread>
#include <future>
#include <chrono>

using nlohmann::json;

static bool test_buffer()
{
    Buffer b;
    std::string s = "hello-buffer";
    b.Append(s);
    if (b.ReadableBytes() != s.size())
        return false;
    std::string out = b.RetrieveAllToStr();
    return out == s;
}

static bool test_hex()
{
    unsigned char data[6] = {0x00, 0x11, 0xAB, 0x7F, 0xFF, 0x42};
    std::string hex = toHex(data, sizeof(data));
    auto bin = fromHex(hex);
    if (bin.size() != sizeof(data))
        return false;
    for (size_t i = 0; i < bin.size(); ++i)
        if (bin[i] != data[i])
            return false;
    return true;
}

static bool test_protocol_response()
{
    json j;
    j["msg"] = "ok";
    j["n"] = 42;
    std::string body = ProtocolResponse::JsonToString(j);
    std::string frame = ProtocolResponse::SerializeResponse(5, body);
    // minimal checks
    if (frame.size() < body.size() + 10)
        return false;
    // verify header: magic
    uint32_t net_magic;
    memcpy(&net_magic, frame.data(), 4);
    uint32_t magic = ntohl(net_magic);
    if (magic != 0x00114514)
        return false;
    // type
    uint16_t net_type;
    memcpy(&net_type, frame.data() + 4, 2);
    if (ntohs(net_type) != 5)
        return false;
    return true;
}

static bool test_protocol_request_parse()
{
    // build a frame and feed into Buffer, then parse with ProtocolRequest
    json j;
    j["user"] = 12345;
    j["action"] = "test_parse";
    std::string body = ProtocolResponse::JsonToString(j);
    std::string frame = ProtocolResponse::SerializeResponse(1, body); // type=1 (LOGIN)

    Buffer b;
    b.Append(frame.data(), frame.size());

    ProtocolRequest req;
    ProtocolRequest::PROTOCOL_RESULT res = req.parse(b);
    if (res != ProtocolRequest::OK)
        return false;
    if (req.type() != 1)
        return false;
    const json &parsed = req.json();
    if (!parsed.is_object())
        return false;
    if (parsed["user"].get<int>() != 12345)
        return false;
    if (parsed["action"].get<std::string>() != "test_parse")
        return false;
    return true;
}

static std::string make_frame(uint16_t type, const nlohmann::json &j)
{
    std::string body = ProtocolResponse::JsonToString(j);
    return ProtocolResponse::SerializeResponse(type, body);
}

static bool test_fragmented_body()
{
    json j;
    j["action"] = "fragmented_body";
    std::string frame = make_frame(5, j); // CHAT

    // split into header+partial body
    size_t hdr = 10;
    size_t part1 = hdr + 3; // header + 3 bytes of body
    Buffer b;
    b.Append(frame.data(), part1);

    ProtocolRequest req;
    auto res1 = req.parse(b);
    if (res1 != ProtocolRequest::NEED_MORE)
        return false;

    // append remainder
    b.Append(frame.data() + part1, frame.size() - part1);
    auto res2 = req.parse(b);
    if (res2 != ProtocolRequest::OK)
        return false;
    return true;
}

static bool test_fragmented_header()
{
    json j;
    j["action"] = "x";
    std::string frame = make_frame(1, j);
    Buffer b;
    // append only 5 bytes (less than header)
    b.Append(frame.data(), 5);
    ProtocolRequest req;
    auto res = req.parse(b);
    // implementation returns ERR_INVALID_MAGIC when header incomplete
    if (res != ProtocolRequest::ERR_INVALID_MAGIC)
        return false;
    return true;
}

static bool test_concatenated_frames()
{
    json j1;
    j1["action"] = "first";
    json j2;
    j2["action"] = "second";
    std::string f1 = make_frame(1, j1);
    std::string f2 = make_frame(5, j2);
    Buffer b;
    b.Append(f1);
    b.Append(f2);

    ProtocolRequest req1;
    if (req1.parse(b) != ProtocolRequest::OK)
        return false;
    if (req1.type() != 1)
        return false;

    ProtocolRequest req2;
    if (req2.parse(b) != ProtocolRequest::OK)
        return false;
    if (req2.type() != 5)
        return false;
    return true;
}

static bool test_bad_magic()
{
    // craft header with wrong magic
    uint32_t bad_magic = htonl(0xDEADBEEF);
    uint16_t net_type = htons(1);
    uint32_t net_len = htonl(0);
    std::string hdr;
    hdr.append(reinterpret_cast<const char *>(&bad_magic), sizeof(bad_magic));
    hdr.append(reinterpret_cast<const char *>(&net_type), sizeof(net_type));
    hdr.append(reinterpret_cast<const char *>(&net_len), sizeof(net_len));

    Buffer b;
    b.Append(hdr);
    ProtocolRequest req;
    auto res = req.parse(b);
    if (res != ProtocolRequest::ERR_INVALID_MAGIC)
        return false;
    return true;
}

static bool test_oversize_len()
{
    // craft header with oversized length (> 4KiB)
    uint32_t magic = htonl(0x00114514);
    uint16_t net_type = htons(1);
    uint32_t net_len = htonl(4 * 1024 + 1);
    std::string hdr;
    hdr.append(reinterpret_cast<const char *>(&magic), sizeof(magic));
    hdr.append(reinterpret_cast<const char *>(&net_type), sizeof(net_type));
    hdr.append(reinterpret_cast<const char *>(&net_len), sizeof(net_len));
    Buffer b;
    b.Append(hdr);
    ProtocolRequest req;
    auto res = req.parse(b);
    if (res != ProtocolRequest::ERR_LEN_INVALID)
        return false;
    return true;
}

static bool test_socket_integration()
{
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0)
        return false;
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0; // ephemeral
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        close(listen_fd);
        return false;
    }
    if (listen(listen_fd, 1) < 0)
    {
        close(listen_fd);
        return false;
    }
    struct sockaddr_in bound{};
    socklen_t blen = sizeof(bound);
    if (getsockname(listen_fd, (struct sockaddr *)&bound, &blen) < 0)
    {
        close(listen_fd);
        return false;
    }
    uint16_t port = ntohs(bound.sin_port);

    std::promise<bool> prom;
    std::future<bool> fut = prom.get_future();

    std::thread srv_thread([listen_fd, p = std::move(prom)]() mutable
                           {
        int conn = accept(listen_fd, nullptr, nullptr);
        if (conn < 0)
        {
            p.set_value(false);
            close(listen_fd);
            return;
        }
        Buffer b;
        char buf[4096];
        ProtocolRequest req;
        while (true)
        {
            ssize_t n = recv(conn, buf, sizeof(buf), 0);
            if (n <= 0)
            {
                p.set_value(false);
                close(conn);
                close(listen_fd);
                return;
            }
            b.Append(buf, n);
            auto res = req.parse(b);
            if (res == ProtocolRequest::OK)
            {
                bool ok = (req.type() == 1);
                if (ok)
                {
                    const auto &j = req.json();
                    ok = j.is_object() && j.value("user", 0) == 777 && j.value("action", "") == "socket_test";
                }
                p.set_value(ok);
                close(conn);
                close(listen_fd);
                return;
            }
            else if (res == ProtocolRequest::NEED_MORE)
            {
                continue;
            }
            else
            {
                p.set_value(false);
                close(conn);
                close(listen_fd);
                return;
            }
        } });

    // client connects and sends framed LOGIN message
    int client = socket(AF_INET, SOCK_STREAM, 0);
    if (client < 0)
    {
        srv_thread.join();
        return false;
    }
    struct sockaddr_in serv{};
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    serv.sin_port = htons(port);
    // small sleep to ensure listener ready
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (connect(client, (struct sockaddr *)&serv, sizeof(serv)) < 0)
    {
        close(client);
        srv_thread.join();
        return false;
    }
    json j;
    j["user"] = 777;
    j["action"] = "socket_test";
    std::string frame = make_frame(1, j);
    ssize_t sent = send(client, frame.data(), frame.size(), 0);
    if (sent != (ssize_t)frame.size())
    {
        close(client);
        srv_thread.join();
        return false;
    }

    // wait for server to parse and report
    auto status = fut.wait_for(std::chrono::seconds(2));
    bool result = false;
    if (status == std::future_status::ready)
    {
        result = fut.get();
    }
    close(client);
    srv_thread.join();
    return result;
}

static bool test_login_register_chat_private_userlist()
{
    // LOGIN
    json login;
    login["username"] = 10000000;
    login["password"] = "password123";
    login["display_name"] = "Alice";
    std::string f_login = make_frame(ProtocolRequest::LOGIN, login);
    Buffer b1;
    b1.Append(f_login);
    ProtocolRequest r1;
    if (r1.parse(b1) != ProtocolRequest::OK)
        return false;
    if (r1.type() != ProtocolRequest::LOGIN)
        return false;

    // REGISTER
    json reg;
    reg["username"] = 10000001;
    reg["password"] = "secret";
    reg["display_name"] = "Bob";
    std::string f_reg = make_frame(ProtocolRequest::REGISTER, reg);
    Buffer b2;
    b2.Append(f_reg);
    ProtocolRequest r2;
    if (r2.parse(b2) != ProtocolRequest::OK)
        return false;

    // CHAT (public)
    json chat;
    chat["username"] = 10000000;
    chat["message"] = "hello everyone";
    std::string f_chat = make_frame(ProtocolRequest::CHAT, chat);
    Buffer b3;
    b3.Append(f_chat);
    ProtocolRequest r3;
    if (r3.parse(b3) != ProtocolRequest::OK)
        return false;

    // PRIVATE_CHAT
    json pchat;
    pchat["from"] = 10000000;
    pchat["to"] = 10000001;
    pchat["message"] = "hi bob";
    std::string f_pchat = make_frame(ProtocolRequest::PRIVATE_CHAT, pchat);
    Buffer b4;
    b4.Append(f_pchat);
    ProtocolRequest r4;
    if (r4.parse(b4) != ProtocolRequest::OK)
        return false;

    // USER_LIST (server->client): we can simulate receiving a USER_LIST frame
    json ulist;
    ulist["users"] = json::array({10000001, 10000002});
    std::string f_ulist = make_frame(ProtocolRequest::USER_LIST, ulist);
    Buffer b5;
    b5.Append(f_ulist);
    ProtocolRequest r5;
    if (r5.parse(b5) != ProtocolRequest::OK)
        return false;

    // basic content checks
    if (r1.json().value("username", 0) != 10000000)
        return false;
    if (r2.json().value("username", 0) != 10000001)
        return false;
    if (r3.json().value("message", std::string{}) != "hello everyone")
        return false;
    if (r4.json().value("to", 0) != 10000001)
        return false;
    if (!r5.json().contains("users"))
        return false;
    return true;
}

int main()
{
    int failures = 0;
    std::cout << "Running component tests...\n";

    if (test_buffer())
        std::cout << "Buffer: PASS\n";
    else
    {
        std::cout << "Buffer: FAIL\n";
        ++failures;
    }
    if (test_hex())
        std::cout << "Hex utils: PASS\n";
    else
    {
        std::cout << "Hex utils: FAIL\n";
        ++failures;
    }
    if (test_protocol_response())
        std::cout << "ProtocolResponse: PASS\n";
    else
    {
        std::cout << "ProtocolResponse: FAIL\n";
        ++failures;
    }
    if (test_protocol_request_parse())
        std::cout << "ProtocolRequest parse: PASS\n";
    else
    {
        std::cout << "ProtocolRequest parse: FAIL\n";
        ++failures;
    }

    if (test_fragmented_body())
        std::cout << "Fragmented body: PASS\n";
    else
    {
        std::cout << "Fragmented body: FAIL\n";
        ++failures;
    }
    if (test_fragmented_header())
        std::cout << "Fragmented header: PASS\n";
    else
    {
        std::cout << "Fragmented header: FAIL\n";
        ++failures;
    }
    if (test_concatenated_frames())
        std::cout << "Concatenated frames: PASS\n";
    else
    {
        std::cout << "Concatenated frames: FAIL\n";
        ++failures;
    }
    if (test_bad_magic())
        std::cout << "Bad magic: PASS\n";
    else
    {
        std::cout << "Bad magic: FAIL\n";
        ++failures;
    }
    if (test_oversize_len())
        std::cout << "Oversize length: PASS\n";
    else
    {
        std::cout << "Oversize length: FAIL\n";
        ++failures;
    }
    if (test_socket_integration())
        std::cout << "Socket integration: PASS\n";
    else
    {
        std::cout << "Socket integration: FAIL\n";
        ++failures;
    }
    if (test_login_register_chat_private_userlist())
        std::cout << "Protocol scenarios: PASS\n";
    else
    {
        std::cout << "Protocol scenarios: FAIL\n";
        ++failures;
    }

    if (failures == 0)
    {
        std::cout << "All component tests passed.\n";
        return 0;
    }
    else
    {
        std::cout << failures << " test(s) failed.\n";
        return 2;
    }
}
