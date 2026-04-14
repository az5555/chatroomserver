// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-06) - brief note
 */

#include "protocolrequest.h"
#include <arpa/inet.h>
#include <cstring>

void ProtocolRequest::Init()
{
    state_ = READ_HEADER;
    type_ = 0;
    len_ = 0;
    body_.clear();
}

ProtocolRequest::PROTOCOL_RESULT ProtocolRequest::parse(Buffer &buff)
{
    if (state_ == READ_HEADER)
    {
        if (!checkMagic(buff))
        {
            return ERR_INVALID_MAGIC;
        }
        type_ = readType(buff);
        if (type_ < 0)
        {
            return ERR_INVALID_MAGIC;
        }
        len_ = readLen(buff);
        if (len_ < 0)
        {
            return ERR_LEN_INVALID;
        }
        if (static_cast<uint32_t>(len_) > MAX_FRAME_SIZE)
        {
            LOG_ERROR("Frame length %d exceeds MAX_FRAME_SIZE %u", len_, MAX_FRAME_SIZE);
            return ERR_LEN_INVALID;
        }
        state_ = READ_BODY;
    }
    if (state_ == READ_BODY)
    {
        json_.clear();
        int ret = readBody(buff, json_);
        if (ret < 0)
        {
            return ERR_PAYLOAD_INVALID;
        }
        else if (ret == 0)
        {
            return NEED_MORE;
        }
        buff.Retrieve(len_ + HEADER_SIZE);
        state_ = FINISH;
    }
    return OK;
}

uint16_t ProtocolRequest::type() const
{
    return type_;
}

uint32_t ProtocolRequest::len() const
{
    return len_;
}

void ProtocolRequest::reset()
{
    Init();
}

bool ProtocolRequest::isComplete() const
{
    return state_ == FINISH;
}

const std::string &ProtocolRequest::body() const
{
    return body_;
}

bool ProtocolRequest::checkMagic(Buffer &buff)
{
    if (buff.ReadableBytes() < HEADER_SIZE)
    {
        return false;
    }
    uint32_t net_magic;
    std::memcpy(&net_magic, buff.peek(), sizeof(net_magic));
    uint32_t host_magic = ntohl(net_magic);
    return host_magic == MAGIC_NUMBER;
}

int ProtocolRequest::readLen(Buffer &buff)
{
    if (buff.ReadableBytes() < HEADER_SIZE)
    {
        return -1;
    }
    uint32_t net_len;
    const char *p = buff.peek() + sizeof(uint32_t) + sizeof(uint16_t);
    std::memcpy(&net_len, p, sizeof(net_len));
    uint32_t host_len = ntohl(net_len);
    return static_cast<int>(host_len);
}

int ProtocolRequest::readType(Buffer &buff)
{
    if (buff.ReadableBytes() < HEADER_SIZE)
    {
        return -1;
    }
    uint16_t net_type;
    const char *p = buff.peek() + sizeof(uint32_t);
    std::memcpy(&net_type, p, sizeof(net_type));
    uint16_t host_type = ntohs(net_type);
    return static_cast<int>(host_type);
}

int ProtocolRequest::readBody(Buffer &buff, nlohmann::json &json)
{
    if (buff.ReadableBytes() < len_ + HEADER_SIZE)
    {
        return 0;
    }
    body_.assign(buff.peek() + HEADER_SIZE, len_);
    // 解析 JSON
    json.clear();
    try
    {
        json = nlohmann::json::parse(body_);
        if (!json.is_object())
        {
            return -1;
        }
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Failed to parse JSON body: %s", e.what());
        return -1;
    }
    return len_;
}

bool ProtocolRequest::resyncToMagic(Buffer &buff)
{
    uint32_t net_magic = htonl(MAGIC_NUMBER);
    while (buff.ReadableBytes() >= sizeof(uint32_t))
    {
        uint32_t net_magec;
        std::memcpy(&net_magec, buff.peek(), sizeof(net_magec));
        if (net_magec == net_magic)
        {
            return true;
        }
        buff.Retrieve(1);
    }
    return false;
}

const nlohmann::json &ProtocolRequest::json() const
{
    return json_;
}