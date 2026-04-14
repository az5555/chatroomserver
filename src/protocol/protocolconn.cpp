// SPDX-License-Identifier: Apache-2.0
/*
 * Original: TinyWebServer-master (author: mark)
 * Modifications: YourName (2026-04-06) - brief note
 */

#include "protocolconn.h"

static std::unordered_map<unsigned long long, std::string> onlineUsers; // 记录在线用户的用户名集合
static std::mutex onlineUsersMutex;                                     // 保护在线用户集合的互斥锁

std::atomic<int> ProtocolConn::userCount{0};
bool ProtocolConn::isET = false;

ProtocolConn::ProtocolConn()
{
    sockFd_ = -1;
    addr_ = {};
    isClose_ = true;
}

ProtocolConn::~ProtocolConn()
{
    Close();
}

void ProtocolConn::Init(int sockFd, const sockaddr_in &addr)
{
    assert(sockFd > 0);
    userCount++;
    addr_ = addr;
    sockFd_ = sockFd;
    writeBuff_.RetrieveAll();
    readBuff_.RetrieveAll();
    isClose_ = false;
    LOG_INFO("Client[%d](%s:%d) in, userCount:%d", sockFd_, GetIP(), GetPort(), (int)userCount);
}

void ProtocolConn::Close()
{
    if (!isClose_)
    {
        isClose_ = true;
        userCount--;
        close(sockFd_);
        LOG_INFO("Client[%d](%s:%d) quit, UserCount:%d", sockFd_, GetIP(), GetPort(), (int)userCount);
    }
}

int ProtocolConn::GetFd() const
{
    return sockFd_;
}

struct sockaddr_in ProtocolConn::GetAddr() const
{
    return addr_;
}

const char *ProtocolConn::GetIP() const
{
    return inet_ntoa(addr_.sin_addr);
}

int ProtocolConn::GetPort() const
{
    return ntohs(addr_.sin_port);
}

size_t ProtocolConn::ToWriteBytes() const
{
    return writeBuff_.ReadableBytes();
}

bool ProtocolConn::IsKeepAlive() const
{
    // 默认协议不支持 keep-alive；如需支持，在解析请求时设置并返回真实值
    return false;
}

bool ProtocolConn::process()
{
    if (readBuff_.ReadableBytes() <= 0)
    {
        return writeBuff_.ReadableBytes() > 0;
    }

    const int MAX_FRAMES_PER_LOOP = 10;
    int handled = 0;

    while (handled < MAX_FRAMES_PER_LOOP && request_.parse(readBuff_) == ProtocolRequest::OK)
    {
        auto res = request_.parse(readBuff_);
        if (res == ProtocolRequest::OK)
        {
            LOG_INFO("Received request: type=%u, len=%u", request_.type(), request_.len());
            DoRequest();
            // 成功消费当前帧，重置解析器准备下一帧
            request_.reset();
            if (readBuff_.ReadableBytes() <= 0)
            {
                break; // 没有更多数据了，退出循环
            }
        }
        else if (res == ProtocolRequest::NEED_MORE)
        {
            // 需要更多数据，等待下一次读
            LOG_DEBUG("Need more data to parse request");
            break;
        }
        else if (res == ProtocolRequest::ERR_INVALID_MAGIC)
        {
            LOG_ERROR("Invalid magic number in request");
            if (request_.resyncToMagic(readBuff_))
            {
                LOG_INFO("Resynchronized to magic number, waiting for more data");
                request_.reset(); // 重置解析器，准备解析下一帧
                continue;         // 继续尝试解析下一帧
            }
            else
            {
                LOG_WARN("Failed to resynchronize to magic number, replying error and resetting parser");
                std::string responseBody = "{\"message\": \"Error!\"}";
                queueResponse(2, responseBody);
                request_.reset();
                continue;
            }
        }
        else
        {
            LOG_WARN("Failed to parse request (code=%d)", static_cast<int>(res));
            std::string responseBody = "{\"message\": \"Error!\"}";
            queueResponse(2, responseBody);
            request_.reset();
            break;
        }
    }

    return writeBuff_.ReadableBytes() > 0;
}

void ProtocolConn::queueResponse(uint16_t type, const std::string &body)
{
    ProtocolResponse::SerializeResponseToBuffer(type, body, writeBuff_);
    LOG_INFO("Queued response: type=%u, len=%zu", type, body.size());
}

void ProtocolConn::queueResponse(uint16_t type, const nlohmann::json &json)
{
    std::string body = ProtocolResponse::JsonToString(json);
    queueResponse(type, body);
}

ssize_t ProtocolConn::read(int *saveErron)
{
    ssize_t len = -1;
    do
    {
        len = readBuff_.ReadFd(sockFd_, saveErron);
        if (len <= 0)
        {
            break;
        }
    } while (isET);
    return len;
}

ssize_t ProtocolConn::write(int *saveErron)
{
    ssize_t len = -1;
    do
    {
        len = writeBuff_.WriteFd(sockFd_, saveErron);
        if (len <= 0)
        {
            break;
        }
    } while (isET);
    return len;
}

void ProtocolConn::DoRequest()
{
    switch (request_.type())
    {
    case ProtocolRequest::LOGIN:
        Login();
        break;
    case ProtocolRequest::LOGOUT:
        Logout();
        break;
    case ProtocolRequest::REGISTER:
        Register();
        break;
    case ProtocolRequest::CHAT:
        Chat();
        break;
    case ProtocolRequest::PRIVATE_CHAT:
        PrivateChat();
        break;
    case ProtocolRequest::USER_LIST:
        UserList();
        break;
    case ProtocolRequest::CHAR_HISTORY:
        CharHistory();
        break;
    case ProtocolRequest::PRIVATE_CHAR_HISTORY:
        PrivateChatHistory();
        break;
    default:
        Error("Unknown request type");
        LOG_ERROR("Received unknown request type: %u", request_.type());
        break;
    }
}

void ProtocolConn::Login()
{
    nlohmann::json requestJson = request_.json();
    if (!requestJson.contains("username") || !requestJson.contains("password"))
    {
        Error("Username and password are required");
        LOG_WARN("Login failed: missing username or password");
        return;
    }
    unsigned long long username = requestJson["username"].get<unsigned long long>();
    std::string password = requestJson["password"].get<std::string>();
    if (password.empty())
    {
        Error("Username and password cannot be empty");
        LOG_WARN("Login failed: empty username or password");
        return;
    }

    MYSQL *conn;
    SqlConnRAII(&conn, SqlConnPool::Instance());
    assert(conn);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    const char *sql = "SELECT password, status, display_name FROM users WHERE username=?";
    if (!stmt || mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to prepare statement: %s", mysql_error(conn));
        if (stmt)
        {
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
        }
        return;
    }

    MYSQL_BIND bind_param;
    memset(&bind_param, 0, sizeof(bind_param));
    bind_param.buffer_type = MYSQL_TYPE_STRING;
    bind_param.buffer = (char *)&username;
    bind_param.buffer_length = sizeof(username);

    if (mysql_stmt_bind_param(stmt, &bind_param) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind parameters: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind_result[3];
    memset(bind_result, 0, sizeof(bind_result));
    char display_name[41] = {0};
    char status = 0;
    char password_[21] = {0};
    unsigned long password_len = 0;
    unsigned long display_name_len = 0;
    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = password_;
    bind_result[0].length = &password_len;

    bind_result[1].buffer_type = MYSQL_TYPE_TINY;
    bind_result[1].buffer = (void *)&status;
    bind_result[1].is_unsigned = 1;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = display_name;
    bind_result[2].length = &display_name_len;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind result: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    int ret = mysql_stmt_fetch(stmt);
    if (ret == MYSQL_NO_DATA)
    {
        Error("NO USER");
        LOG_WARN("Login failed: user not found");
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }
    else
    {
        Error("Database error");
        LOG_ERROR("fetch error: %s", mysql_stmt_error(stmt));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    std::string stored_password(password_, password_len);
    std::string stored_display_name(display_name, display_name_len);

    if (stored_password != password)
    {
        Error("WRONG PASSWORD");
        LOG_WARN("Login failed: wrong password for user '%llu'", username);
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    if (status == 0)
    {
        Error("USER DISABLED");
        LOG_WARN("Login failed: user '%llu' is disabled", username);
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    unsigned char token[32];
    if (RAND_bytes(token, sizeof(token)) != 1)
    {
        Error("Failed to generate token");
        LOG_ERROR("Failed to generate token");
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    std::string tokenHex = toHex(token, sizeof(token));
    // TODO: 将 token 存储到redis，并关联到用户会话
    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (onlineUsers.find(username) != onlineUsers.end())
        {
            nlohmann::json responseJson;
            responseJson["msg"] = "USER ALREADY LOGGED IN";
            responseJson["token"] = onlineUsers[username];
            queueResponse(ProtocolRequest::ACK, responseJson);
            LOG_WARN("Login failed: user '%llu' is already logged in", username);
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
            return;
        }
        else
        {
            onlineUsers.insert({username, tokenHex});
        }
    }
    nlohmann::json responseJson;
    responseJson["token"] = tokenHex;
    responseJson["msg"] = "login successful";
    queueResponse(ProtocolRequest::ACK, responseJson);
    LOG_INFO("User '%llu' logged in successfully", username);

    const char *updateSql = "UPDATE users SET last_login=NOW() WHERE username=?";
    if (mysql_stmt_prepare(stmt, updateSql, strlen(updateSql)) != 0)
    {
        LOG_ERROR("Failed to prepare update statement: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    if (mysql_stmt_bind_param(stmt, &bind_param) != 0)
    {
        LOG_ERROR("Failed to bind parameters for update: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        LOG_ERROR("Failed to execute update statement: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

void ProtocolConn::Logout()
{
    nlohmann::json responseJson = request_.json();
    if (!responseJson.contains("username") || !responseJson.contains("token"))
    {
        Error("Username and token are required");
        LOG_WARN("Logout failed: missing username or token");
        return;
    }
    unsigned long long username = responseJson["username"].get<unsigned long long>();
    std::string token = responseJson["token"].get<std::string>();
    if (token.empty())
    {
        Error("Username and token cannot be empty");
        LOG_WARN("Logout failed: empty username or token");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (onlineUsers.count(username) > 0 || onlineUsers[username] != token)
        {
            Error("ERROR LOGOUT");
            LOG_WARN("Logout failed: user '%llu' is not logged in", username);
            return;
        }
        else
        {
            onlineUsers.erase(username);
            std::string responseBody = "{\"msg\": \"logout successful\"}";
            queueResponse(ProtocolRequest::ACK, responseBody);
            LOG_INFO("User '%llu' logged out successfully", username);
        }
    }
}

void ProtocolConn::Register()
{
    nlohmann::json requestJson = request_.json();
    if (!requestJson.contains("username") || !requestJson.contains("password") || !requestJson.contains("display_name"))
    {
        Error("Username, password and display name are required");
        LOG_WARN("Registration failed: missing username, password or display name");
        return;
    }
    unsigned long long username = requestJson["username"].get<unsigned long long>();
    std::string password = requestJson["password"].get<std::string>();
    std::string displayName = requestJson["display_name"].get<std::string>();

    if (password.empty() || displayName.empty())
    {
        Error("Username, password and display name cannot be empty");
        LOG_WARN("Registration failed: empty username, password or display name");
        return;
    }

    if (displayName.size() > 40)
    {
        Error("Display name cannot exceed 40 characters");
        LOG_WARN("Registration failed: display name too long for user");
        return;
    }

    if (username >= 10000000 || username <= 1000000)
    {
        Error("Invalid username");
        LOG_WARN("Registration failed: invalid username '0'");
        return;
    }

    if (password.size() > 20)
    {
        Error("Password cannot exceed 20 characters");
        LOG_WARN("Registration failed: password too long for user '%llu'", username);
        return;
    }

    if (password.size() < 6)
    {
        Error("Password must be at least 6 characters");
        LOG_WARN("Registration failed: password too short for user '%llu'", username);
        return;
    }
    auto isValidPassword = [](const std::string &pwd)
    {
        for (char c : pwd)
        {
            if (!std::isalnum(static_cast<unsigned char>(c)))
                return false;
        }
        return true;
    };

    if (!isValidPassword(password))
    {
        Error("Password cannot contain special characters");
        LOG_WARN("Registration failed: password contains special characters");
        return;
    }

    MYSQL *conn;
    SqlConnRAII(&conn, SqlConnPool::Instance());
    assert(conn);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    const char *sql = "INSERT INTO users (username, password, display_name) VALUES (?, ?, ?)";

    MYSQL_BIND bind_param[3];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = &username;
    bind_param[0].buffer_length = sizeof(username);

    bind_param[1].buffer_type = MYSQL_TYPE_STRING;
    bind_param[1].buffer = (char *)password.c_str();
    bind_param[1].buffer_length = password.size();

    bind_param[2].buffer_type = MYSQL_TYPE_STRING;
    bind_param[2].buffer = (char *)displayName.c_str();
    bind_param[2].buffer_length = displayName.size();

    if (!stmt || mysql_stmt_prepare(stmt, sql, strlen(sql)))
    {
        Error("Database error");
        LOG_ERROR("Failed to prepare statement: %s", mysql_error(conn));
        if (stmt)
        {
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
        }
        return;
    }

    if (mysql_stmt_bind_param(stmt, bind_param) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind parameters: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        if (mysql_errno(conn) == ER_DUP_ENTRY)
        {
            Error("Username or display name already exists");
            LOG_WARN("Registration failed: username '%llu' or display name '%s' already exists", username, displayName.c_str());
        }
        else
        {
            Error("Database error");
            LOG_ERROR("Failed to execute statement: %s", mysql_error(conn));
        }
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }
    std::string responseBody = "{\"msg\": \"registration successful\"}";
    queueResponse(ProtocolRequest::ACK, responseBody);
    LOG_INFO("User '%llu' registered successfully", username);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

void ProtocolConn::Chat()
{
    nlohmann::json requestJson = request_.json();
    if (!requestJson.contains("username") || !requestJson.contains("msg") || !requestJson.contains("token"))
    {
        Error("Username, message and token are required");
        LOG_WARN("Chat failed: missing username, message or token");
        return;
    }
    unsigned long long username = requestJson["username"].get<unsigned long long>();
    std::string msg = requestJson["msg"].get<std::string>();
    std::string token = requestJson["token"].get<std::string>();

    if (msg.empty() || token.empty())
    {
        Error("Username, message and token cannot be empty");
        LOG_WARN("Chat failed: empty username, message or token");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (onlineUsers.count(username) > 0 || onlineUsers[username] != token)
        {
            Error("Invalid request");
            LOG_WARN("Invalid chat request from user '%llu'", username);
            return;
        }
    }
    MYSQL *conn;
    SqlConnRAII(&conn, SqlConnPool::Instance());
    assert(conn);

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    const char *sql = "INSERT INTO public_messages (username, message) VALUES (?, ?)";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to prepare statement: %s", mysql_error(conn));
        if (stmt)
        {
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
        }
        return;
    }

    MYSQL_BIND bind_param[2];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = &username;
    bind_param[0].buffer_length = sizeof(username);

    bind_param[1].buffer_type = MYSQL_TYPE_STRING;
    bind_param[1].buffer = (char *)msg.c_str();
    bind_param[1].buffer_length = msg.size();

    if (mysql_stmt_bind_param(stmt, bind_param) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind parameters: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to execute statement: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    std::string responseBody = "{\"msg\": \"message sent\"}";
    queueResponse(ProtocolRequest::ACK, responseBody);
    LOG_INFO("User '%llu' sent a public message", username);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

void ProtocolConn::PrivateChat()
{
    nlohmann::json requestJson = request_.json();
    if (!requestJson.contains("username") || !requestJson.contains("to_username") || !requestJson.contains("msg") || !requestJson.contains("token"))
    {
        Error("Username, recipient username, message and token are required");
        LOG_WARN("Private chat failed: missing username, recipient username, message or token");
        return;
    }
    unsigned long long username = requestJson["username"].get<unsigned long long>();
    unsigned long long toUsername = requestJson["to_username"].get<unsigned long long>();
    std::string msg = requestJson["msg"].get<std::string>();
    std::string token = requestJson["token"].get<std::string>();
    if (msg.empty() || token.empty())
    {
        Error("Username, message and token cannot be empty");
        LOG_WARN("Private chat failed: empty username, message or token");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (onlineUsers.count(username) > 0 || onlineUsers[username] != token)
        {
            Error("Invalid request");
            LOG_WARN("Invalid request");
            return;
        }
    }
    MYSQL *conn;
    SqlConnRAII(&conn, SqlConnPool::Instance());
    assert(conn);
    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    const char *sql = "INSERT INTO private_messages (from_username, to_username, message) VALUES (?, ?, ?)";

    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to prepare statement: %s", mysql_error(conn));
        if (stmt)
        {
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
        }
        return;
    }

    MYSQL_BIND bind_param[3];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = &username;
    bind_param[0].buffer_length = sizeof(username);

    bind_param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[1].buffer = &toUsername;
    bind_param[1].buffer_length = sizeof(toUsername);

    bind_param[2].buffer_type = MYSQL_TYPE_STRING;
    bind_param[2].buffer = (char *)msg.c_str();
    bind_param[2].buffer_length = msg.size();

    if (mysql_stmt_bind_param(stmt, bind_param) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind parameters: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to execute statement: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    std::string responseBody = "{\"msg\": \"message sent\"}";
    queueResponse(ProtocolRequest::ACK, responseBody);
    LOG_INFO("User '%llu' sent a private message to user '%llu'", username, toUsername);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

void ProtocolConn::UserList()
{
    nlohmann::json requestJson = request_.json();
    if (!requestJson.contains("username") || !requestJson.contains("token"))
    {
        Error("Username and token are required");
        LOG_WARN("User list request failed: missing username or token");
        return;
    }
    unsigned long long username = requestJson["username"].get<unsigned long long>();
    std::string token = requestJson["token"].get<std::string>();
    if (token.empty())
    {
        Error("Username and token cannot be empty");
        LOG_WARN("User list request failed: empty username or token");
        return;
    }

    MYSQL *conn;
    SqlConnRAII(&conn, SqlConnPool::Instance());
    assert(conn);

    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (onlineUsers.count(username) > 0 || onlineUsers[username] != token)
        {
            Error("Invalid request");
            LOG_WARN("Invalid user list request from user '%llu'", username);
            return;
        }
    }

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    const char *sql = "SELECT username, display_name, last_login FROM users";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to prepare statement: %s", mysql_error(conn));
        if (stmt)
        {
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
        }
        return;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to execute statement: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind_result[3];
    memset(bind_result, 0, sizeof(bind_result));
    unsigned long long userId;
    char display_name[41] = {0};
    unsigned long display_name_len = 0;
    char last_login[20] = {0};
    unsigned long last_login_len = 0;
    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &userId;
    bind_result[0].buffer_length = sizeof(userId);

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = display_name;
    bind_result[1].length = &display_name_len;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = last_login;
    bind_result[2].length = &last_login_len;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind result: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    nlohmann::json usersJson = nlohmann::json::array();
    while (mysql_stmt_fetch(stmt) == 0)
    {
        nlohmann::json userJson;
        userJson["username"] = userId;
        userJson["display_name"] = std::string(display_name, display_name_len);
        userJson["last_login"] = std::string(last_login, last_login_len);
        {
            std::lock_guard<std::mutex> lock(onlineUsersMutex);
            userJson["online"] = onlineUsers.count(userId) > 0;
        }
        usersJson.push_back(userJson);
    }

    nlohmann::json responseJson;
    responseJson["users"] = usersJson;
    queueResponse(ProtocolRequest::ACK, responseJson);
    LOG_INFO("User '%llu' requested user list", username);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

void ProtocolConn::Error(const std::string &msg)
{
    nlohmann::json responseJson;
    responseJson["msg"] = msg;
    queueResponse(ProtocolRequest::ERROR, responseJson);
}

void ProtocolConn::Ack(const std::string &msg)
{
    nlohmann::json responseJson;
    responseJson["msg"] = msg;
    queueResponse(ProtocolRequest::ACK, responseJson);
}

void ProtocolConn::CharHistory()
{
    nlohmann::json requestJson = request_.json();
    if (!requestJson.contains("username") || !requestJson.contains("token"))
    {
        Error("Username and token are required");
        LOG_WARN("Chat history request failed: missing username or token");
        return;
    }

    unsigned long long username = requestJson["username"].get<unsigned long long>();
    std::string token = requestJson["token"].get<std::string>();
    if (token.empty())
    {
        Error("Username and token cannot be empty");
        LOG_WARN("Chat history request failed: empty username or token");
        return;
    }

    MYSQL *conn;
    SqlConnRAII(&conn, SqlConnPool::Instance());
    assert(conn);

    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (onlineUsers.count(username) > 0 || onlineUsers[username] != token)
        {
            Error("Invalid request");
            LOG_WARN("Invalid chat history request from user '%llu'", username);
            return;
        }
    }

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    const char *sql = "SELECT from_username, message, created_at "
                      "FROM public_messages "
                      "ORDER BY created_at DESC";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to prepare statement: %s", mysql_error(conn));
        if (stmt)
        {
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
        }
        return;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to execute statement: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind_result[3];
    memset(bind_result, 0, sizeof(bind_result));
    unsigned long long fromUsername;
    char message[513] = {0};
    unsigned long message_len = 0;
    char created_at[20] = {0};
    unsigned long created_at_len = 0;

    bind_result[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[0].buffer = &fromUsername;
    bind_result[0].buffer_length = sizeof(fromUsername);

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = message;
    bind_result[1].length = &message_len;

    bind_result[2].buffer_type = MYSQL_TYPE_STRING;
    bind_result[2].buffer = created_at;
    bind_result[2].length = &created_at_len;

    if (mysql_stmt_bind_result(stmt, bind_result) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind result: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    nlohmann::json messagesJson = nlohmann::json::array();
    while (mysql_stmt_fetch(stmt) == 0)
    {
        nlohmann::json messageJson;
        messageJson["from_username"] = fromUsername;
        messageJson["msg"] = std::string(message, message_len);
        messageJson["created_at"] = std::string(created_at, created_at_len);
        messagesJson.push_back(messageJson);
    }
    nlohmann::json responseJson;
    responseJson["messages"] = messagesJson;
    queueResponse(ProtocolRequest::ACK, responseJson);
    LOG_INFO("User '%llu' requested chat history", username);
    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);
}

void ProtocolConn::PrivateChatHistory()
{
    nlohmann::json requestJson = request_.json();
    if (!requestJson.contains("username") || !requestJson.contains("to_username") || !requestJson.contains("token"))
    {
        Error("Username, recipient username and token are required");
        LOG_WARN("Private chat history request failed: missing username, recipient username or token");
        return;
    }
    unsigned long long username = requestJson["username"].get<unsigned long long>();
    unsigned long long toUsername = requestJson["to_username"].get<unsigned long long>();
    std::string token = requestJson["token"].get<std::string>();

    if (token.empty())
    {
        Error("Username, recipient username and token cannot be empty");
        LOG_WARN("Private chat history request failed: empty username, recipient username or token");
        return;
    }

    MYSQL *conn;
    SqlConnRAII(&conn, SqlConnPool::Instance());
    assert(conn);

    {
        std::lock_guard<std::mutex> lock(onlineUsersMutex);
        if (onlineUsers.count(username) > 0 || onlineUsers[username] != token)
        {
            Error("Invalid request");
            LOG_WARN("Invalid private chat history request from user '%llu'", username);
            return;
        }
    }

    MYSQL_STMT *stmt = mysql_stmt_init(conn);
    const char *sql = "SELECT message, created_at, from_username "
                      "FROM private_messages "
                      "WHERE (from_username=? AND to_username=?) OR (from_username=? AND to_username=?) "
                      "ORDER BY created_at DESC";
    if (mysql_stmt_prepare(stmt, sql, strlen(sql)) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to prepare statement: %s", mysql_error(conn));
        if (stmt)
        {
            mysql_stmt_free_result(stmt);
            mysql_stmt_close(stmt);
        }
        return;
    }

    MYSQL_BIND bind_param[4];
    memset(bind_param, 0, sizeof(bind_param));
    bind_param[0].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[0].buffer = &username;
    bind_param[0].buffer_length = sizeof(username);

    bind_param[1].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[1].buffer = &toUsername;
    bind_param[1].buffer_length = sizeof(toUsername);

    bind_param[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[2].buffer = &toUsername;
    bind_param[2].buffer_length = sizeof(toUsername);

    bind_param[3].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_param[3].buffer = &username;
    bind_param[3].buffer_length = sizeof(username);

    if (mysql_stmt_bind_param(stmt, bind_param) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind parameters: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    if (mysql_stmt_execute(stmt) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to execute statement: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    MYSQL_BIND bind_result[3];

    memset(bind_result, 0, sizeof(bind_result));
    char message[513] = {0};
    unsigned long message_len = 0;
    char created_at[20] = {0};
    unsigned long created_at_len = 0;
    unsigned long long fromUsername;

    bind_result[0].buffer_type = MYSQL_TYPE_STRING;
    bind_result[0].buffer = message;
    bind_result[0].length = &message_len;

    bind_result[1].buffer_type = MYSQL_TYPE_STRING;
    bind_result[1].buffer = created_at;
    bind_result[1].length = &created_at_len;

    bind_result[2].buffer_type = MYSQL_TYPE_LONGLONG;
    bind_result[2].buffer = &fromUsername;
    bind_result[2].buffer_length = sizeof(fromUsername);

    if (mysql_stmt_bind_result(stmt, bind_result) != 0)
    {
        Error("Database error");
        LOG_ERROR("Failed to bind result: %s", mysql_error(conn));
        mysql_stmt_free_result(stmt);
        mysql_stmt_close(stmt);
        return;
    }

    nlohmann::json messagesJson = nlohmann::json::array();
    while (mysql_stmt_fetch(stmt) == 0)
    {
        nlohmann::json messageJson;
        messageJson["msg"] = std::string(message, message_len);
        messageJson["created_at"] = std::string(created_at, created_at_len);
        messageJson["username"] = fromUsername;
        messagesJson.push_back(messageJson);
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    nlohmann::json responseJson;
    responseJson["messages"] = messagesJson;
    queueResponse(ProtocolRequest::ACK, responseJson);
    LOG_INFO("User '%llu' requested private chat history with user '%llu'", username,
             toUsername);
}
