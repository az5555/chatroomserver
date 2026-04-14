// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#ifndef SQLCONNPOLL_H
#define SQLCONNPOLL_H

#include <mysql/mysql.h>
#include <string>
#include <queue>
#include <mutex>
#include <semaphore.h>
#include <thread>
#include "../log/log.h"

class SqlConnPool
{
public:
    static SqlConnPool *Instance();

    MYSQL *GetConn();
    void FreeConn(MYSQL *conn);
    int GetFreeConnCount();
    void Init(const char *host, int port,
              const char *user, const char *pwd,
              const char *dbName, int connSize);
    void ClosePool();

private:
    SqlConnPool();
    ~SqlConnPool();

    int freeCount_;
    int MAX_CONN_;
    int useCount_;
    std::queue<MYSQL *> connQue_;
    std::mutex mtx_;
    sem_t semId_;
};
#endif // SQLCONNPOLL_H