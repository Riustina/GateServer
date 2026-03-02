// VerifyGrpcClient.cpp

#include "VerifyGrpcClient.h"
#include "global.h"
#include "ConfigManager.h"

// ==================== RPCConnectionPool ====================

RPCConnectionPool::RPCConnectionPool(size_t poolSize, std::string host, std::string port) :
	poolSize_(poolSize), host_(host), port_(port) {
	for (size_t i = 0; i < poolSize_; ++i) {
		// 创建一个gRPC通道,连接到服务器的地址和端口
		std::shared_ptr<Channel> channel = grpc::CreateChannel(host_ + ":" + port_, grpc::InsecureChannelCredentials());
		// 使用通道创建一个VerifyService的stub对象,并将其添加到连接池中
		connections_.emplace(std::make_unique<VerifyService::Stub>(channel));
	}
}

// 析构函数,关闭连接池并释放资源
RPCConnectionPool::~RPCConnectionPool() {
	std::lock_guard<std::mutex> lock(mutex_);
	isShutdown_ = true;
	cv_.notify_all();	// 通知所有等待线程连接池已关闭
	while (!connections_.empty()) {
		connections_.pop();
	}
}

// 获取一个连接,如果连接池已关闭则返回nullptr
std::unique_ptr<VerifyService::Stub> RPCConnectionPool::getConnection() {
	std::unique_lock<std::mutex> lock(mutex_);
	cv_.wait(lock, [this]() { return !connections_.empty() || isShutdown_; });
	if (isShutdown_) {
		return nullptr;
	}

	auto connection = std::move(connections_.front());
	connections_.pop();
	return connection;
}

// 释放一个连接,将其放回连接池中
void RPCConnectionPool::releaseConnection(std::unique_ptr<VerifyService::Stub> connection) {
	std::lock_guard<std::mutex> lock(mutex_);
	// 如果连接池未关闭,将连接放回连接池中,并通知一个等待线程有可用连接
	if (!isShutdown_) {
		connections_.push(std::move(connection));
		cv_.notify_one();	// 通知一个等待线程有可用连接
	}
	// 如果连接池已关闭,连接将被销毁,不放回连接池
}


// ==================== VerifyGrpcClient ====================

VerifyGrpcClient::VerifyGrpcClient() {
	auto &configManager = ConfigManager::getInstance();	// 获取配置管理器实例
	std::string host = configManager["VerifyServer"]["host"];	// 从配置文件中获取VerifyServer的主机地址
	std::string port = configManager["VerifyServer"]["port"];	// 从配置文件中获取VerifyServer的端口号
	connectionPool_ = std::make_unique<RPCConnectionPool>(5, host, port);	// 创建一个RPCConnectionPool对象,连接池大小为5
}

GetVerifyRsp VerifyGrpcClient::GetVerifyCode(std::string email) {
	ClientContext context;	// 创建一个ClientContext对象,用于设置RPC调用的上下文信息
	GetVerifyRsp reply;
	GetVerifyReq request;

	request.set_email(email);	// 设置请求消息的email字段
	auto stub = connectionPool_->getConnection();	// 从连接池中获取一个VerifyService的stub对象
	Status status = stub->GetVerifyCode(&context, request, &reply);	// 调用RPC方法,传入上下文、请求消息和响应消息

	if (!status.ok()) {	// 如果RPC调用失败,设置响应消息的错误码,并输出错误信息
		connectionPool_->releaseConnection(std::move(stub));	// 释放连接,将stub对象放回连接池中
		reply.set_error(ErrorCodes::RPC_Failed);
		std::cerr << "[VerifyGrpcClient.cpp] 函数 [GetVerifyCode] RPC failed: " << status.error_message() << "\n" << std::endl;
		return reply;
	}

	connectionPool_->releaseConnection(std::move(stub));	// 释放连接,将stub对象放回连接池中
	return reply;
}