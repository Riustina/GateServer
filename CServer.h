#pragma once

#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <boost/asio.hpp>

#include <memory>

class CServer : public std::enable_shared_from_this<CServer>
{
public:
	CServer(boost::asio::io_context& ioc, unsigned short& port);	// 构造函数，初始化成员变量
	void Start();													// 启动服务器，开始监听端口

private:
	boost::asio::ip::tcp::acceptor _acceptor;						// TCP接收器，用于监听和接受连接
	boost::asio::io_context& _ioc;									// 引用io_context对象，用于异步操作
	boost::asio::ip::tcp::socket _socket;							// TCP套接字，用于与客户端通信

};

