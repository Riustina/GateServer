// VerifyGrpcClient.h

#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "global.h"
#include "Singleton.h"
#include <queue>
#include <memory>
#include <condition_variable>


using grpc::Channel;		// gRPC通道类，用于创建和管理与服务器的连接
using grpc::Status;			// gRPC状态类，用于表示RPC调用的结果状态
using grpc::ClientContext;	// gRPC客户端上下文类，用于设置RPC调用的上下文信息，如超时、元数据等

using message::GetVerifyReq;	// 从message.proto生成的GetVerifyReq消息类，用于构建RPC请求
using message::GetVerifyRsp;	// 从message.proto生成的GetVerifyRsp消息类，用于接收RPC响应
using message::VerifyService;	// 从message.proto生成的VerifyService服务类，用于定义RPC方法的接口

class RPCConnectionPool {
public:
	RPCConnectionPool(size_t poolSize, std::string host, std::string port) :
		poolSize_(poolSize), host_(host), port_(port) {
		for (size_t i = 0; i < poolSize_; ++i) {
			// 创建一个gRPC通道，连接到服务器的地址和端口
			std::shared_ptr<Channel> channel = grpc::CreateChannel(host_ + ":" + port_, grpc::InsecureChannelCredentials());
			// 使用通道创建一个VerifyService的stub对象，并将其添加到连接池中
			connections_.emplace(std::make_unique<VerifyService::Stub>(channel));
		}
	}

	// 析构函数，关闭连接池并释放资源
	~RPCConnectionPool() {
		std::lock_guard<std::mutex> lock(mutex_);
		isShutdown_ = true;
		cv_.notify_all();	// 通知所有等待线程连接池已关闭
		while (!connections_.empty()) {
			connections_.pop();
		}
	}

	// 获取一个连接，如果连接池已关闭则返回nullptr
	std::unique_ptr<VerifyService::Stub> getConnection() {
		std::unique_lock<std::mutex> lock(mutex_);
		cv_.wait(lock, [this]() { return !connections_.empty() || isShutdown_; });
		if (isShutdown_) {
			return nullptr;
		}
		auto connection = std::move(connections_.front());
		connections_.pop();
		return connection;
	}

	// 释放一个连接，将其放回连接池中
	void releaseConnection(std::unique_ptr<VerifyService::Stub> connection) {
		std::lock_guard<std::mutex> lock(mutex_);
		// 如果连接池未关闭，将连接放回连接池中，并通知一个等待线程有可用连接
		if (!isShutdown_) {
			connections_.push(std::move(connection));
			cv_.notify_one();	// 通知一个等待线程有可用连接
		}
		// 如果连接池已关闭，连接将被销毁，不放回连接池
	}

private:
	std::atomic<bool> isShutdown_{ false };	// 原子布尔变量，表示连接池是否已关闭，初始值为false
	size_t poolSize_;	// 连接池的大小，即连接的数量
	std::string host_;	// 服务器的主机地址
	std::string port_;	// 服务器的端口号

	std::queue<std::unique_ptr<VerifyService::Stub>> connections_;	// 存储VerifyService的stub对象的队列，用于调用RPC方法
	std::condition_variable cv_;
	std::mutex mutex_;
};

class VerifyGrpcClient : public Singleton<VerifyGrpcClient>
{
	friend class Singleton<VerifyGrpcClient>;	// 让Singleton<VerifyGrpcClient>成为VerifyGrpcClient的友元类，可以访问其私有成员
public:
	GetVerifyRsp GetVerifyCode(std::string email) {
		ClientContext context;	// 创建一个ClientContext对象，用于设置RPC调用的上下文信息
		GetVerifyRsp reply;
		GetVerifyReq request;
		request.set_email(email);	// 设置请求消息的email字段

		auto stub = connectionPool_->getConnection();	// 从连接池中获取一个VerifyService的stub对象
		Status status = stub->GetVerifyCode(&context, request, &reply);	// 调用RPC方法，传入上下文、请求消息和响应消息

		if (!status.ok()) {	// 如果RPC调用失败，设置响应消息的错误码，并输出错误信息
			connectionPool_->releaseConnection(std::move(stub));	// 释放连接，将stub对象放回连接池中
			reply.set_error(ErrorCodes::RPC_Failed);
			std::cerr << "[VerifyGrpcClient.h] 函数 [GetVerifyCode] RPC failed: " << status.error_message() << std::endl;
			return reply;
		}

		connectionPool_->releaseConnection(std::move(stub));	// 释放连接，将stub对象放回连接池中
		return reply;
	}

private:
	VerifyGrpcClient() {};
	std::unique_ptr<RPCConnectionPool> connectionPool_;	// 连接池对象，用于管理RPC连接
};

