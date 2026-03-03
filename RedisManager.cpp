// RedisManager.cpp

#include "RedisManager.h"
#include <hiredis/hiredis.h>
#include <iostream>
#include <cstring>  // strcmp

#ifdef _WIN32
#include <winsock2.h>
#endif

// ─────────────────────────────────────────────
// 构造 / 析构
// ─────────────────────────────────────────────

RedisManager::RedisManager() : _connect(nullptr), _port(0) {}

RedisManager::~RedisManager()
{
    Close();
}

// ─────────────────────────────────────────────
// Connect / Reconnect / Close
// ─────────────────────────────────────────────

bool RedisManager::Connect(const std::string& host, int port)
{
    // 释放旧连接，防止内存泄漏
    if (_connect != nullptr) {
        redisFree(_connect);
        _connect = nullptr;
    }

    struct timeval timeout = { 1, 500000 }; // 1.5 秒超时
    _connect = redisConnectWithTimeout(host.c_str(), port, timeout);

    if (_connect == nullptr) {
        std::cerr << "[RedisManager.cpp] Connect [Connect] 内存分配失败，无法创建连接对象\n";
        return false;
    }
    if (_connect->err) {
        std::cerr << "[RedisManager.cpp] Connect [Connect] 连接失败: " << _connect->errstr << "\n";
        redisFree(_connect);
        _connect = nullptr;
        return false;
    }

    // 保存参数，供 Reconnect() 使用
    _host = host;
    _port = port;

    std::cout << "[RedisManager.cpp] Connect [Connect] 连接成功 -> " << host << ":" << port << "\n";
    return true;
}

bool RedisManager::Reconnect()
{
    if (_host.empty() || _port == 0) {
        std::cerr << "[RedisManager.cpp] Reconnect [Reconnect] 尚未调用过 Connect，无法重连\n";
        return false;
    }
    std::cerr << "[RedisManager.cpp] Reconnect [Reconnect] 连接已断开，尝试重连 "
        << _host << ":" << _port << " ...\n";
    return Connect(_host, _port);
}

void RedisManager::Close()
{
    if (_connect != nullptr) {
        redisFree(_connect);
        _connect = nullptr;
        std::cout << "[RedisManager.cpp] Close [Close] 连接已安全关闭\n";
    }
}

// ─────────────────────────────────────────────
// 内部辅助宏：执行命令后检查 reply 是否为空
// 若为空则说明连接已断，尝试重连并重试一次
// ─────────────────────────────────────────────
//
// 之所以用宏而非模板/lambda，是因为 redisCommand 是可变参数 C 函数，
// 不能用普通函数指针统一封装；宏可以原封不动地展开调用。
//
#define REDIS_CMD_WITH_RETRY(reply_var, ...)                        \
    redisReply* reply_var =                                         \
        (redisReply*)redisCommand(_connect, __VA_ARGS__);           \
    if ((reply_var) == nullptr) {                                   \
        if (Reconnect()) {                                          \
            (reply_var) =                                           \
                (redisReply*)redisCommand(_connect, __VA_ARGS__);   \
        }                                                           \
    }

// ─────────────────────────────────────────────
// Auth
// ─────────────────────────────────────────────

bool RedisManager::Auth(const std::string& password)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "AUTH %b",
        password.c_str(), (size_t)password.length());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] Auth [AUTH] 命令执行失败：无法与 Redis 通信\n";
        return false;
    }

    bool success = false;
    if (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0) {
        std::cout << "[RedisManager.cpp] Auth [AUTH] 认证成功\n";
        success = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] Auth [AUTH] 认证失败: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] Auth [AUTH] 未预期的返回类型: " << reply->type << "\n";
    }

    freeReplyObject(reply);
    return success;
}

// ─────────────────────────────────────────────
// GET / SET
// ─────────────────────────────────────────────

bool RedisManager::Get(const std::string& key, std::string& value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "GET %b",
        key.c_str(), (size_t)key.length());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] Get [GET] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);   // 二进制安全赋值
        std::cout << "[RedisManager.cpp] Get [GET] 成功，Key: " << key << "\n";
        success = true;
        break;
    case REDIS_REPLY_NIL:
        // Key 不存在是正常业务情况，不是错误
        std::cout << "[RedisManager.cpp] Get [GET] Key 不存在: " << key << "\n";
        value.clear();
        success = false;
        break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] Get [GET] Redis 返回错误: " << reply->str << "\n";
        success = false;
        break;
    default:
        std::cerr << "[RedisManager.cpp] Get [GET] 未预期的返回类型: " << reply->type << "\n";
        success = false;
    }

    freeReplyObject(reply);
    return success;
}

bool RedisManager::Set(const std::string& key, const std::string& value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "SET %b %b",
        key.c_str(), (size_t)key.length(),
        value.c_str(), (size_t)value.length());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] Set [SET] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    if (reply->type == REDIS_REPLY_STATUS && strcmp(reply->str, "OK") == 0) {
        std::cout << "[RedisManager.cpp] Set [SET] 成功，Key: " << key << "\n";
        success = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] Set [SET] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] Set [SET] 未预期的返回类型: " << reply->type << "\n";
    }

    freeReplyObject(reply);
    return success;
}

// ─────────────────────────────────────────────
// LPUSH / LPOP / RPUSH / RPOP
// ─────────────────────────────────────────────

bool RedisManager::LPush(const std::string& key, const std::string& value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "LPUSH %b %b",
        key.c_str(), (size_t)key.length(),
        value.c_str(), (size_t)value.length());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] LPush [LPUSH] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        // reply->integer 为 Push 后列表的总长度，>= 1 才真正 Push 了数据
        std::cout << "[RedisManager.cpp] LPush [LPUSH] 成功，Key: " << key
            << "，当前列表长度: " << reply->integer << "\n";
        success = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] LPush [LPUSH] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] LPush [LPUSH] 未预期的返回类型: " << reply->type << "\n";
    }

    freeReplyObject(reply);
    return success;
}

bool RedisManager::LPop(const std::string& key, std::string& value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "LPOP %b",
        key.c_str(), (size_t)key.length());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] LPop [LPOP] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);
        std::cout << "[RedisManager.cpp] LPop [LPOP] 成功，Key: " << key << "\n";
        success = true;
        break;
    case REDIS_REPLY_NIL:
        // 队列为空是正常业务情况
        std::cout << "[RedisManager.cpp] LPop [LPOP] 队列为空，Key: " << key << "\n";
        success = false;
        break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] LPop [LPOP] Redis 返回错误: " << reply->str << "\n";
        success = false;
        break;
    default:
        std::cerr << "[RedisManager.cpp] LPop [LPOP] 未预期的返回类型: " << reply->type << "\n";
        success = false;
    }

    freeReplyObject(reply);
    return success;
}

bool RedisManager::RPush(const std::string& key, const std::string& value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "RPUSH %b %b",
        key.c_str(), (size_t)key.length(),
        value.c_str(), (size_t)value.length());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] RPush [RPUSH] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        std::cout << "[RedisManager.cpp] RPush [RPUSH] 成功，Key: " << key
            << "，当前列表长度: " << reply->integer << "\n";
        success = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] RPush [RPUSH] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] RPush [RPUSH] 未预期的返回类型: " << reply->type << "\n";
    }

    freeReplyObject(reply);
    return success;
}

bool RedisManager::RPop(const std::string& key, std::string& value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "RPOP %b",
        key.c_str(), (size_t)key.length());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] RPop [RPOP] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);
        std::cout << "[RedisManager.cpp] RPop [RPOP] 成功，Key: " << key << "\n";
        success = true;
        break;
    case REDIS_REPLY_NIL:
        std::cout << "[RedisManager.cpp] RPop [RPOP] 队列为空，Key: " << key << "\n";
        success = false;
        break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] RPop [RPOP] Redis 返回错误: " << reply->str << "\n";
        success = false;
        break;
    default:
        std::cerr << "[RedisManager.cpp] RPop [RPOP] 未预期的返回类型: " << reply->type << "\n";
        success = false;
    }

    freeReplyObject(reply);
    return success;
}

// ─────────────────────────────────────────────
// HSET / HGET
// ─────────────────────────────────────────────

bool RedisManager::HSet(std::string_view key, std::string_view field, std::string_view value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "HSET %b %b %b",
        key.data(), key.size(),
        field.data(), field.size(),
        value.data(), value.size());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] HSet [HSET] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        // 0 = 字段已存在并被更新，1 = 新字段被插入，两者都算成功
        std::cout << "[RedisManager.cpp] HSet [HSET] 成功，Key: " << key
            << "，Field: " << field
            << (reply->integer == 1 ? "（新增）" : "（更新）") << "\n";
        success = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] HSet [HSET] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] HSet [HSET] 未预期的返回类型: " << reply->type << "\n";
    }

    freeReplyObject(reply);
    return success;
}

bool RedisManager::HGet(std::string_view key, std::string_view field, std::string& value)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "HGET %b %b",
        key.data(), key.size(),
        field.data(), field.size());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] HGet [HGET] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    switch (reply->type) {
    case REDIS_REPLY_STRING:
        value.assign(reply->str, reply->len);
        std::cout << "[RedisManager.cpp] HGet [HGET] 成功，Key: " << key
            << "，Field: " << field << "\n";
        success = true;
        break;
    case REDIS_REPLY_NIL:
        // Key 或 Field 不存在，属于正常业务情况
        std::cout << "[RedisManager.cpp] HGet [HGET] Field 不存在，Key: " << key
            << "，Field: " << field << "\n";
        success = false;
        break;
    case REDIS_REPLY_ERROR:
        std::cerr << "[RedisManager.cpp] HGet [HGET] Redis 返回错误: " << reply->str << "\n";
        success = false;
        break;
    default:
        std::cerr << "[RedisManager.cpp] HGet [HGET] 未预期的返回类型: " << reply->type << "\n";
        success = false;
    }

    freeReplyObject(reply);
    return success;
}

// ─────────────────────────────────────────────
// DEL / EXISTS
// ─────────────────────────────────────────────

bool RedisManager::Del(std::string_view key)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "DEL %b",
        key.data(), key.size());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] Del [DEL] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool success = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        // reply->integer = 实际被删除的 Key 数量
        // 0 表示 Key 不存在，但命令本身成功执行，业务上仍视为成功
        if (reply->integer > 0) {
            std::cout << "[RedisManager.cpp] Del [DEL] 成功删除，Key: " << key << "\n";
        }
        else {
            std::cout << "[RedisManager.cpp] Del [DEL] Key 不存在（无需删除），Key: " << key << "\n";
        }
        success = true;
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] Del [DEL] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] Del [DEL] 未预期的返回类型: " << reply->type << "\n";
    }

    freeReplyObject(reply);
    return success;
}

bool RedisManager::ExistsKey(std::string_view key)
{
    if (_connect == nullptr && !Reconnect()) return false;

    REDIS_CMD_WITH_RETRY(reply, "EXISTS %b",
        key.data(), key.size());

    if (reply == nullptr) {
        std::cerr << "[RedisManager.cpp] ExistsKey [EXISTS] 命令执行失败，Key: " << key << "\n";
        return false;
    }

    bool found = false;
    if (reply->type == REDIS_REPLY_INTEGER) {
        // 单 Key 查询：0 = 不存在，1 = 存在
        found = (reply->integer > 0);
        if (found) {
            std::cout << "[RedisManager.cpp] ExistsKey [EXISTS] Key 存在: " << key << "\n";
        }
        else {
            std::cout << "[RedisManager.cpp] ExistsKey [EXISTS] Key 不存在: " << key << "\n";
        }
    }
    else if (reply->type == REDIS_REPLY_ERROR) {
        std::cerr << "[RedisManager.cpp] ExistsKey [EXISTS] Redis 返回错误: " << reply->str << "\n";
    }
    else {
        std::cerr << "[RedisManager.cpp] ExistsKey [EXISTS] 未预期的返回类型: " << reply->type << "\n";
    }

    freeReplyObject(reply);
    return found;
}