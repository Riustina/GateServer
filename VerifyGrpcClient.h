// VerifyGrpcClient.h

#pragma once
#include <grpcpp/grpcpp.h>
#include "message.grpc.pb.h"
#include "global.h"
#include "Singleton.h"


using grpc::Channel;		// gRPC通道类，用于创建和管理与服务器的连接
using grpc::Status;			// gRPC状态类，用于表示RPC调用的结果状态
using grpc::ClientContext;	// gRPC客户端上下文类，用于设置RPC调用的上下文信息，如超时、元数据等

using message::GetVerifyReq;	// 从message.proto生成的GetVerifyReq消息类，用于构建RPC请求
using message::GetVerifyRsp;	// 从message.proto生成的GetVerifyRsp消息类，用于接收RPC响应
using message::VerifyService;	// 从message.proto生成的VerifyService服务类，用于定义RPC方法的接口

class VerifyGrpcClient : public Singleton<VerifyGrpcClient>
{
	friend class Singleton<VerifyGrpcClient>;	// 让Singleton<VerifyGrpcClient>成为VerifyGrpcClient的友元类，可以访问其私有成员
public:
	GetVerifyRsp GetVerifyCode(std::string email) {
		ClientContext context;	// 创建一个ClientContext对象，用于设置RPC调用的上下文信息
		GetVerifyRsp reply;
		GetVerifyReq request;
		request.set_email(email);	// 设置请求消息的email字段

		Status status = stub_->GetVerifyCode(&context, request, &reply);	// 调用RPC方法，传入上下文、请求消息和响应消息

		if (!status.ok()) {	// 如果RPC调用失败，设置响应消息的错误码，并输出错误信息
			reply.set_error(ErrorCodes::RPC_Failed);
			std::cerr << "[VerifyGrpcClient.h] 函数 [GetVerifyCode] RPC failed: " << status.error_message() << std::endl;
			return reply;
		}
		return reply;
	}

private:
	VerifyGrpcClient() {
		std::shared_ptr<Channel> channel = grpc::CreateChannel("0.0.0.0:50051", 
			grpc::InsecureChannelCredentials());	// 创建一个gRPC通道，连接到服务器的地址和端口
		stub_ = VerifyService::NewStub(channel);	// 使用通道创建一个VerifyService的stub对象，用于调用RPC方法 
	}
	std::unique_ptr<VerifyService::Stub> stub_;	// 存储VerifyService的stub对象，用于调用RPC方法

};

