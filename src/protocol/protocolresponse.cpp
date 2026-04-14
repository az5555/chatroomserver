// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-06) - brief note
 */

#include "protocolresponse.h"

void ProtocolResponse::SerializeResponseToBuffer(uint16_t type, const std::string &body, Buffer &buff)
{
    std::string response = SerializeResponse(type, body);
    buff.EnsureWriteable(response.size());
    buff.Append(response);
}

std::string ProtocolResponse::SerializeResponse(uint16_t type, const std::string &body)
{
    if (body.size() > MAX_FRAME_SIZE)
    {
        LOG_ERROR("Response body size %zu exceeds MAX_FRAME_SIZE %u", body.size(), MAX_FRAME_SIZE);
        return "";
    }
    std::string response;
    uint32_t len = body.size();
    uint32_t net_magic = htonl(MAGIC_NUMBER);
    uint16_t net_type = htons(type);
    uint32_t net_len = htonl(len);
    response.append(reinterpret_cast<const char *>(&net_magic), sizeof(net_magic));
    response.append(reinterpret_cast<const char *>(&net_type), sizeof(net_type));
    response.append(reinterpret_cast<const char *>(&net_len), sizeof(net_len));
    response.append(body);
    return response;
}

void ProtocolResponse::Init()
{
}

std::string ProtocolResponse::JsonToString(const nlohmann::json &json)
{
    return json.dump();
}
