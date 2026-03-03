// RedisManager.h

#pragma once
#include "Singleton.h"
#include <string>
#include <string_view>

// 前向声明，避免在头文件中直接包含 hiredis
struct redisContext;

class RedisManager : public Singleton<RedisManager>
{
    friend class Singleton<RedisManager>;
public:
    ~RedisManager();

    bool Connect(const std::string& host, int port);

    bool Get(const std::string& key, std::string& value);
    bool Set(const std::string& key, const std::string& value);
    bool Auth(const std::string& password);

    bool LPush(const std::string& key, const std::string& value);
    bool LPop(const std::string& key, std::string& value);
    bool RPush(const std::string& key, const std::string& value);
    bool RPop(const std::string& key, std::string& value);

    bool HSet(std::string_view key, std::string_view field, std::string_view value);
    bool HGet(std::string_view key, std::string_view field, std::string& value);

    bool Del(std::string_view key);
    bool ExistsKey(std::string_view key);

    void Close();

private:
    RedisManager();

    // 尝试重新建立连接（使用上一次保存的 host/port）
    bool Reconnect();

    redisContext* _connect = nullptr;

    // 保存连接参数，供 Reconnect() 使用
    std::string _host;
    int         _port = 0;

    // 禁止拷贝（Singleton 语义）
    RedisManager(const RedisManager&) = delete;
    RedisManager& operator=(const RedisManager&) = delete;
};