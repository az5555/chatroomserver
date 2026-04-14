// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#include "buffer.h"

Buffer::Buffer(int initBuffSize) : buffer_(initBuffSize),
                                   readPos_(0), writePos_(0) {};

size_t Buffer::WritebleBytes() const
{
    return buffer_.size() - writePos_;
}

size_t Buffer::ReadableBytes() const
{
    return writePos_ - readPos_;
}

size_t Buffer::PrependableBytes() const
{
    return readPos_;
}

const char *Buffer::peek() const
{
    return BeginPtr_() + readPos_;
}

const char *Buffer::BeginWriteConst() const
{
    return BeginPtr_() + writePos_;
}

char *Buffer::BeginWrite()
{
    return BeginPtr_() + writePos_;
}

void Buffer::EnsureWriteable(size_t len)
{
    if (WritebleBytes() < len)
    {
        MakeSpace_(len);
    }
    assert(WritebleBytes() >= len);
}

void Buffer::HasWritten(size_t len)
{
    writePos_ += len;
}

void Buffer::Retrieve(size_t len)
{
    assert(len <= ReadableBytes());
    readPos_ += len;
}

void Buffer::RetrieveUntil(const char *end)
{
    assert(peek() <= end);
    Retrieve(end - peek());
}

void Buffer::RetrieveAll()
{
    bzero(&buffer_[0], buffer_.size());
    readPos_ = 0;
    writePos_ = 0;
}

void Buffer::Append(const std::string &str)
{
    Append(str.data(), str.length());
}

void Buffer::Append(const char *str, size_t len)
{
    assert(str);
    Append(static_cast<const void *>(str), len);
}

void Buffer::Append(const void *data, size_t len)
{
    assert(data);
    EnsureWriteable(len);
    std::copy(static_cast<const char *>(data), static_cast<const char *>(data) + len, BeginWrite());
    HasWritten(len);
}

void Buffer::Append(const Buffer &buff)
{
    Append(buff.peek(), buff.ReadableBytes());
}

ssize_t Buffer::ReadFd(int fd, int *Errno)
{
    char buff[65536];
    struct iovec iov[2];
    const size_t writeable = WritebleBytes();
    iov[0].iov_base = BeginWrite();
    iov[0].iov_len = writeable;
    iov[1].iov_base = buff;
    iov[1].iov_len = sizeof(buff);
    const ssize_t len = readv(fd, iov, 2);
    if (len < 0)
    {
        *Errno = errno;
    }
    else if (static_cast<size_t>(len) <= writeable)
    {
        HasWritten(len);
    }
    else
    {
        HasWritten(writeable);
        Append(buff, len - writeable);
    }
    return len;
}

ssize_t Buffer::WriteFd(int fd, int *Errno)
{
    size_t readSize = ReadableBytes();
    ssize_t len = write(fd, peek(), readSize);
    if (len < 0)
    {
        *Errno = errno;
    }
    else
    {
        Retrieve(len);
    }
    return len;
}

char *Buffer::BeginPtr_()
{
    return &*buffer_.begin();
}

const char *Buffer::BeginPtr_() const
{
    return &*buffer_.begin();
}

void Buffer::MakeSpace_(size_t len)
{
    if (WritebleBytes() + PrependableBytes() < len)
    {
        buffer_.resize(writePos_ + len);
    }
    else
    {
        size_t readable = ReadableBytes();
        std::copy(BeginPtr_() + readPos_, BeginPtr_() + writePos_, BeginPtr_());
        readPos_ = 0;
        writePos_ = readPos_ + readable;
        assert(readable == ReadableBytes());
    }
}

std::string Buffer::RetrieveAllToStr()
{
    std::string str(peek(), ReadableBytes());
    RetrieveAll();
    return str;
}