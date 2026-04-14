// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */
#ifndef SERVER_H
#define SERVER_H

#include <unordered_map>
#include <fcntl.h>  // fcntl()
#include <unistd.h> // close()
#include <assert.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "epoller.h"
#include "../log/log.h"
#include "../timer/heaptimer.h"
#include "../pool/sqlconnpool.h"
#include "../pool/threadpool.h"
#include "../pool/sqlconnRAII.h"
#include "../protocol/protocolconn.h"

class Server
{
public:
    Server(
        int port, int trigMode, int timeoutMS, bool OptLinger,
        int sqlPort, const char *sqlUser, const char *sqlPwd,
        const char *dbName, int connPoolNum, int threadNum,
        bool openLog, int logLevel, int logQueSize);

    ~Server();
    void Start();

private:
    bool InitSocket_();

    void InitEventMode_(int trigMode);

    void AddClient_(int fd, sockaddr_in addr);

    void DealListen_();

    void SendError_(int fd, const char *info);

    void ExtentTimer_(ProtocolConn *client);

    void CloseConn_(ProtocolConn *client);

    void OnRead_(ProtocolConn *client);

    void OnWrite_(ProtocolConn *client);

    void OnProcess(ProtocolConn *client);

    void DealRead_(ProtocolConn *client);

    void DealWrite_(ProtocolConn *client);

    void ExtentTime_(ProtocolConn *client);

    static int SetFdNonblock(int fd);

private:
    static const int MAX_FD = 65536;

    int port_;

    bool OptLinger_;

    int timeoutMS_;

    bool isClose_;

    int listenFd_;

    uint32_t listenEvent_;

    uint32_t connEvent_;

    std::unique_ptr<HeapTimer> timer_;

    std::unique_ptr<ThreadPool> threadpool_;

    std::unique_ptr<Epoller> epoller_;

    std::unordered_map<int, ProtocolConn> clients_;
};

#endif // SERVER_H