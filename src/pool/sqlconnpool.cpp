// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-05) - brief note
 */

#include "sqlconnpool.h"

SqlConnPool::SqlConnPool() : useCount_(0), freeCount_(0), MAX_CONN_(0) {}

SqlConnPool::~SqlConnPool()
{
    ClosePool();
}

SqlConnPool *SqlConnPool::Instance()
{
    static SqlConnPool sqlConnPool;
    return &sqlConnPool;
}

MYSQL *SqlConnPool::GetConn()
{
    MYSQL *conn = nullptr;
    if (connQue_.empty())
    {
        LOG_WARN("SqlConnPool busy!");
        return nullptr;
    }
    sem_wait(&semId_);
    {
        std::lock_guard<std::mutex> locker(mtx_);
        conn = connQue_.front();
        connQue_.pop();
        useCount_++;
    }
    return conn;
}

void SqlConnPool::FreeConn(MYSQL *conn)
{
    assert(conn);
    std::lock_guard<std::mutex> locker(mtx_);
    connQue_.push(conn);
    useCount_--;
    sem_post(&semId_);
}

int SqlConnPool::GetFreeConnCount()
{
    std::lock_guard<std::mutex> locker(mtx_);
    return connQue_.size();
}

void SqlConnPool::Init(const char *host, int port,
                       const char *user, const char *pwd,
                       const char *dbName, int connSize)
{
    assert(connSize > 0);
    for (int i = 0; i < connSize; i++)
    {
        MYSQL *sql = nullptr;
        sql = mysql_init(sql);
        if (!sql)
        {
            LOG_ERROR("MySql init error!");
            assert(sql);
        }
        sql = mysql_real_connect(sql, host, user, pwd,
                                 dbName, port, nullptr, 0);
        if (!sql)
        {
            LOG_ERROR("MySql Connect error!");
        }
        connQue_.push(sql);
    }
    MAX_CONN_ = connSize;
    sem_init(&semId_, 0, MAX_CONN_);
}

void SqlConnPool::ClosePool()
{
    std::lock_guard<std::mutex> locker(mtx_);
    while (!connQue_.empty())
    {
        MYSQL *sql = connQue_.front();
        connQue_.pop();
        mysql_close(sql);
    }
    mysql_library_end();
    sem_destroy(&semId_);
}
