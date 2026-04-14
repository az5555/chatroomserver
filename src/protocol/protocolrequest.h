// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#ifndef PROTOCOL_REQUEST_H
#define PROTOCOL_REQUEST_H

#include <unordered_map>
#include <unordered_set>
#include <string>
#include <regex>
#include <errno.h>
#include <mysql/mysql.h> //mysql

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "../pool/sqlconnpool.h"
#include "../pool/sqlconnRAII.h"
#include "json.hpp"

class ProtocolRequest
{
public:
    enum PARSE_STATE
    {
        READ_HEADER,
        READ_BODY,
        FINISH,
    };

    enum PROTOCOL_RESULT
    {
        OK = 0,
        NEED_MORE,
        ERR_INVALID_MAGIC,
        ERR_LEN_INVALID,
        ERR_PAYLOAD_INVALID,
    };

    enum PROTOCOL_TYPE
    {
        LOGIN = 1,
        LOGOUT = 2,
        REGISTER = 3,
        ACK = 4,
        CHAT = 5,
        PRIVATE_CHAT = 6,
        USER_LIST = 7,
        ERROR = 8,
        CHAR_HISTORY = 9,
        PRIVATE_CHAR_HISTORY = 10,
    };

    ProtocolRequest() { Init(); };

    ~ProtocolRequest() = default;

    void Init();

    PROTOCOL_RESULT parse(Buffer &buff);

    uint16_t type() const;

    uint32_t len() const;

    const nlohmann::json &json() const;

    void reset();

    bool isComplete() const;

    const std::string &body() const;

    bool resyncToMagic(Buffer &buff);

private:
    int readLen(Buffer &buff);

    int readType(Buffer &buff);

    int readBody(Buffer &buff, nlohmann::json &json);

    bool checkMagic(Buffer &buff);

private:
    PARSE_STATE state_;
    uint16_t type_;
    uint32_t len_;
    std::string body_;

    static const uint32_t MAGIC_NUMBER = 0x00114514;
    // magic(4) + type(2) + len(4) = 10 bytes
    static const size_t HEADER_SIZE = 10;
    // maximum allowed payload length (4 KiB)
    static const uint32_t MAX_FRAME_SIZE = 4 * 1024;
    nlohmann::json json_;
};

#endif /* PROTOCOL_REQUEST_H */