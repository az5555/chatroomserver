// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#ifndef EPOLLER_H
#define EPOLLER_H

#include <sys/epoll.h> //epoll_ctl()
#include <fcntl.h>     // fcntl()
#include <unistd.h>    // close()
#include <assert.h>    // close()
#include <vector>
#include <errno.h>

class Epoller
{
public:
    explicit Epoller(int maxEvent = 1024);

    ~Epoller();

    bool AddFd(int fd, uint32_t events);

    bool ModFd(int fd, uint32_t events);

    bool DelFd(int fd);

    int Wait(int timeoutMs = -1);

    int GetEventFd(size_t index) const;

    uint32_t GetEvents(size_t index) const;

private:
    std::vector<epoll_event> events_;

    int epollFd_;
};

#endif // EPOLLER_H