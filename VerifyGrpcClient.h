// VerifyGrpcClient.h

#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "Singleton.h"
#include <queue>
#include <memory>
#include <condition_variable>

using grpc::Channel;		// gRPC通道类,用于创建和管理与服务器的连接
using grpc::Status;			// gRPC状态类,用于表示RPC调用的结果状态
using grpc::ClientContext;	// gRPC客户端上下文类,用于设置RPC调用的上下文信息,如超时、元数据等

using message::GetVerifyReq;	// 从message.proto生成的GetVerifyReq消息类,用于构建RPC请求
using message::GetVerifyRsp;	// 从message.proto生成的GetVerifyRsp消息类,用于接收RPC响应
using message::VerifyService;	// 从message.proto生成的VerifyService服务类,用于定义RPC方法的接口

class RPCConnectionPool {
public:
	RPCConnectionPool(size_t poolSize, std::string host, std::string port);	// 析构函数,关闭连接池并释放资源
	~RPCConnectionPool();	// 获取一个连接,如果连接池已关闭则返回nullptr

	std::unique_ptr<VerifyService::Stub> getConnection();	// 释放一个连接,将其放回连接池中
	void releaseConnection(std::unique_ptr<VerifyService::Stub> connection);

private:
	std::atomic<bool> isShutdown_{ false };	// 原子布尔变量,表示连接池是否已关闭,初始值为false
	size_t poolSize_;	// 连接池的大小,即连接的数量
	std::string host_;	// 服务器的主机地址
	std::string port_;	// 服务器的端口号

	std::queue<std::unique_ptr<VerifyService::Stub>> connections_;	// 存储VerifyService的stub对象的队列,用于调用RPC方法
	std::condition_variable cv_;
	std::mutex mutex_;

};

class VerifyGrpcClient : public Singleton<VerifyGrpcClient>
{
	friend class Singleton<VerifyGrpcClient>;	// 让Singleton<VerifyGrpcClient>成为VerifyGrpcClient的友元类,可以访问其私有成员
public:
	GetVerifyRsp GetVerifyCode(std::string email);
private:
	VerifyGrpcClient();
	std::unique_ptr<RPCConnectionPool> connectionPool_;	// 连接池对象,用于管理RPC连接
};