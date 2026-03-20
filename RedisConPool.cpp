#include "RedisConPool.h"

#include <hiredis/hiredis.h>
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#endif

// ==================== 构造：初始化连接池并完成认证 ====================

RedisConPool::RedisConPool(size_t poolSize,
    const std::string& host,
    int                port,
    const std::string& pwd)
    : poolSize_(poolSize)
    , host_(host)
    , port_(port)
    , pwd_(pwd)
    , b_stop_(false)
{
    for (size_t i = 0; i < poolSize_; ++i) {
        redisContext* ctx = createAuthenticatedConnection();
        if (ctx != nullptr) {
            connections_.push(ctx);
        }
    }

    if (connections_.empty()) {
        std::cerr << "[RedisConPool.cpp] RedisConPool [构造] 警告：没有任何可用连接\n";
    } else {
        std::cout << "[RedisConPool.cpp] RedisConPool [构造] 初始化完成，可用连接数: "
                  << connections_.size() << "/" << poolSize_ << "\n";
    }
}

// ==================== 析构 ====================

RedisConPool::~RedisConPool()
{
    Close();
}

// ==================== getConnection：获取连接 ====================

redisContext* RedisConPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] {
        return b_stop_.load() || !connections_.empty();
    });

    if (b_stop_.load()) {
        return nullptr;
    }

    redisContext* ctx = connections_.front();
    connections_.pop();
    return ctx;
}

// ==================== returnConnection：归还连接，必要时自动重建 ====================

void RedisConPool::returnConnection(redisContext* ctx)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (b_stop_.load()) {
        if (ctx != nullptr) {
            redisFree(ctx);
        }
        return;
    }

    if (ctx == nullptr || ctx->err != 0) {
        std::cerr << "[RedisConPool.cpp] returnConnection [归还] 检测到损坏连接，尝试重建\n";
        if (ctx != nullptr) {
            redisFree(ctx);
        }
        redisContext* newCtx = createAuthenticatedConnection();
        if (newCtx != nullptr) {
            connections_.push(newCtx);
            cond_.notify_one();
        }
        return;
    }

    connections_.push(ctx);
    cond_.notify_one();
}

// ==================== Close：关闭连接池并释放所有资源 ====================

void RedisConPool::Close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_.load()) {
            return;
        }
        b_stop_.store(true);

        while (!connections_.empty()) {
            redisFree(connections_.front());
            connections_.pop();
        }
    }

    cond_.notify_all();
    std::cout << "[RedisConPool.cpp] Close [Close] 连接池已关闭，所有连接已释放\n";
}

// ==================== availableCount：返回当前可用连接数 ====================

size_t RedisConPool::availableCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}

// ==================== createAuthenticatedConnection：创建并认证连接 ====================

redisContext* RedisConPool::createAuthenticatedConnection()
{
    struct timeval timeout = { 1, 500000 }; // 1.5 秒超时
    redisContext* ctx = redisConnectWithTimeout(host_.c_str(), port_, timeout);

    if (ctx == nullptr) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [创建] 内存分配失败\n";
        return nullptr;
    }
    if (ctx->err != 0) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [创建] 连接失败: "
                  << ctx->errstr << "\n";
        redisFree(ctx);
        return nullptr;
    }

    redisReply* reply = (redisReply*)redisCommand(
        ctx, "AUTH %b", pwd_.c_str(), (size_t)pwd_.length());

    if (reply == nullptr) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [AUTH] 未收到响应\n";
        redisFree(ctx);
        return nullptr;
    }

    bool authed = (reply->type == REDIS_REPLY_STATUS &&
                   strcmp(reply->str, "OK") == 0);

    if (!authed) {
        std::cerr << "[RedisConPool.cpp] createAuthenticatedConnection [AUTH] 认证失败: "
                  << (reply->str ? reply->str : "未知原因") << "\n";
        freeReplyObject(reply);
        redisFree(ctx);
        return nullptr;
    }

    freeReplyObject(reply);
    return ctx;
}
