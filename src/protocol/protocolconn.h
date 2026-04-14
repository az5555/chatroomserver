// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#ifndef PROCOL_CONN_H
#define PROCOL_CONN_H

#include <sys/types.h>
#include <sys/uio.h>   // readv/writev
#include <arpa/inet.h> // sockaddr_in
#include <stdlib.h>    // atoi()
#include <errno.h>
#include <string.h> // memset()
#include <mysql/mysqld_error.h>

#include "../log/log.h"
#include "../pool/sqlconnRAII.h"
#include "../buffer/buffer.h"
#include "protocolrequest.h"
#include "protocolresponse.h"
#include "utils.h"

class ProtocolConn
{
public:
    ProtocolConn();

    ~ProtocolConn();

    void Init(int sockFd, const sockaddr_in &addr);

    ssize_t read(int *saveErrno);

    ssize_t write(int *saveErrno);

    void Close();

    int GetFd() const;

    int GetPort() const;

    const char *GetIP() const;

    sockaddr_in GetAddr() const;

    bool process();

    void queueResponse(uint16_t type, const std::string &body);

    void queueResponse(uint16_t type, const nlohmann::json &json);

    // 返回待写入的字节数
    size_t ToWriteBytes() const;
    // 是否保持长连接（若协议不支持，返回 false）
    bool IsKeepAlive() const;

    static bool isET;
    static std::atomic<int> userCount;

    void DoRequest();

    void Login();

    void Logout();

    void Register();

    void Chat();

    void PrivateChat();

    void UserList();

    void Ack(const std::string &message);

    void Error(const std::string &message);

    void CharHistory();

    void PrivateChatHistory();

private:
    int sockFd_;
    struct sockaddr_in addr_;
    bool isClose_;
    ProtocolRequest request_;

    int iovCnt_;

    Buffer writeBuff_;
    Buffer readBuff_;

    std::string response_;
};

#endif /* PROCOL_CONN_H */