// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#ifndef BUFFER_H
#define BUFFER_H
#include <cstring>
#include <iostream>
#include <vector>
#include <unistd.h>
#include <sys/uio.h>
#include <atomic>
#include <assert.h>

class Buffer
{
public:
    Buffer(int initBuffSize = 1024);
    ~Buffer() = default;

    size_t WritebleBytes() const;
    size_t ReadableBytes() const;
    size_t PrependableBytes() const;

    // ptr
    const char *peek() const;
    const char *BeginWriteConst() const;
    char *BeginWrite();

    // ensure
    void EnsureWriteable(size_t len);
    void HasWritten(size_t len);

    // drop
    void Retrieve(size_t len);
    void RetrieveUntil(const char *end);
    void RetrieveAll();
    std::string RetrieveAllToStr();

    void Append(const std::string &str);
    void Append(const char *str, size_t len);
    void Append(const void *data, size_t len);
    void Append(const Buffer &buff);

    ssize_t ReadFd(int fd, int *Errno);
    ssize_t WriteFd(int fd, int *Errno);

private:
    char *BeginPtr_();
    const char *BeginPtr_() const;
    void MakeSpace_(size_t len);

    std::vector<char> buffer_;
    std::atomic<std::size_t> readPos_;
    std::atomic<std::size_t> writePos_;
};

#endif // BUFFER_H