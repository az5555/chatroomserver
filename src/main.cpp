// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-04) - brief note
 */

#include "server/server.h"

int main(int argc, char *argv[])
{

    Server server(
        1316, 3, 60000, false,             /* 端口 ET模式 timeoutMs 优雅退出  */
        3306, "root", "root", "webserver", /* Mysql配置 */
        12, 6, true, 1, 1024);             /* 连接池数量 线程池数量 日志开关 日志等级 日志异步队列容量 */
    server.Start();
}