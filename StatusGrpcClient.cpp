// StatusGrpcClient.cpp

#include "StatusGrpcClient.h"
#include <iostream>
#include "Defer.h"

// ══════════════════════════════════════════════
// StatusConPool
// ══════════════════════════════════════════════

StatusConPool::StatusConPool(size_t poolSize,
    const std::string& host,
    const std::string& port)
    : poolSize_(poolSize)
    , b_stop_(false)
{
    const std::string address = host + ":" + port;
    for (size_t i = 0; i < poolSize_; ++i) {
        auto channel = grpc::CreateChannel(address, grpc::InsecureChannelCredentials());
        connections_.push(StatusService::NewStub(channel));
    }
    std::cout << "[StatusGrpcClient.cpp] StatusConPool [构造] 连接池初始化完成，"
        << "地址: " << address << "，连接数: " << poolSize_ << "\n";
}

StatusConPool::~StatusConPool()
{
    Close();
}

std::unique_ptr<StatusService::Stub> StatusConPool::getConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] {
        return b_stop_.load() || !connections_.empty();
        });

    if (b_stop_.load()) {
        std::cerr << "[StatusGrpcClient.cpp] getConnection [获取连接] 连接池已关闭\n";
        return nullptr;
    }

    auto stub = std::move(connections_.front());
    connections_.pop();
    return stub;
}

void StatusConPool::returnConnection(std::unique_ptr<StatusService::Stub> stub)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (b_stop_.load()) {
        // 池已关闭，stub 随 unique_ptr 析构自动释放
        std::cerr << "[StatusGrpcClient.cpp] returnConnection [归还连接] 连接池已关闭，丢弃连接\n";
        return;
    }

    connections_.push(std::move(stub));
    cond_.notify_one();
}

void StatusConPool::Close()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (b_stop_.load()) return; // 防止重复关闭
        b_stop_.store(true);

        // 清空队列，unique_ptr 析构时自动释放 stub
        while (!connections_.empty()) {
            connections_.pop();
        }
    }
    cond_.notify_all();
    std::cout << "[StatusGrpcClient.cpp] Close [Close] 连接池已关闭，所有连接已释放\n";
}

// ══════════════════════════════════════════════
// StatusGrpcClient
// ══════════════════════════════════════════════

StatusGrpcClient::StatusGrpcClient()
{
    auto& cfg = ConfigManager::getInstance();
    const std::string host = cfg["StatusServer"]["host"];
    const std::string port = cfg["StatusServer"]["port"];
    pool_ = std::make_unique<StatusConPool>(5, host, port);
}

GetChatServerRsp StatusGrpcClient::GetChatServer(int uid)
{
    GetChatServerRsp reply;

    // 1. 取出连接，取不到说明池已关闭
    auto stub = pool_->getConnection();
    if (stub == nullptr) {
        std::cerr << "[StatusGrpcClient.cpp] GetChatServer [GetChatServer] "
            << "获取 gRPC 连接失败，uid: " << uid << "\n";
        reply.set_error(ErrorCodes::RPC_Failed);
        return reply;
    }

    // 2. 取到连接后立即注册归还，确保任何路径下都能归还
    Defer defer([&stub, this]() {
        pool_->returnConnection(std::move(stub));
        });

    // 3. 发起 RPC 调用
    GetChatServerReq request;
    request.set_uid(uid);
    ClientContext context;

    Status status = stub->GetChatServer(&context, request, &reply);
    if (status.ok()) {
        std::cout << "[StatusGrpcClient.cpp] GetChatServer [GetChatServer] "
            << "分配 ChatServer 成功，uid: " << uid
            << "，host: " << reply.host() << "\n";
        return reply;
    }

    std::cerr << "[StatusGrpcClient.cpp] GetChatServer [GetChatServer] "
        << "gRPC 调用失败，uid: " << uid
        << "，错误: " << status.error_message() << "\n";
    reply.set_error(ErrorCodes::RPC_Failed);
    return reply;
}
