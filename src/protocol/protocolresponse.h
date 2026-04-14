// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#ifndef PROTOCOL_RESPONSE_H
#define PROTOCOL_RESPONSE_H

#include <unordered_map>
#include <fcntl.h>    // open
#include <unistd.h>   // close
#include <sys/stat.h> // stat
#include <sys/mman.h> // mmap, munmap
#include <arpa/inet.h>

#include "../buffer/buffer.h"
#include "../log/log.h"
#include "json.hpp"

class ProtocolResponse
{
public:
    ProtocolResponse() { Init(); };

    ~ProtocolResponse() = default;

    void Init();

    static std::string JsonToString(const nlohmann::json &json);

    static std::string SerializeResponse(uint16_t type, const std::string &body);

    static void SerializeResponseToBuffer(uint16_t type, const std::string &body, Buffer &buff);

private:
    static const int MAX_FRAME_SIZE = 4 * 1024; // 4 KiB
    static const uint32_t MAGIC_NUMBER = 0x00114514;
};

#endif /* PROTOCOL_RESPONSE_H */
