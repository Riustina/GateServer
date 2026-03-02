// HttpConnection.h

#pragma once
#include <boost/beast/http.hpp>
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <memory>
#include <unordered_map>

class HttpConnection : public std::enable_shared_from_this<HttpConnection>
{
public:
	friend class LogicSystem;	// 让LogicSystem类成为HttpConnection的友元类，可以访问其私有成员
	HttpConnection(boost::asio::ip::tcp::socket socket);	// 构造函数，接受一个TCP套接字
	void Start();											// 启动连接，开始处理请求
private:
	void CheckDeadLine();									// 检查连接是否超时
	void WriteResponse();									// 向客户端发送响应
	void HandleRequest();									// 处理客户端请求
	void PreParseGetParam();								// 预解析GET请求的参数，提取URL中的查询字符串并解析成键值对

	boost::asio::ip::tcp::socket _socket;					// TCP套接字，用于与客户端通信
	boost::beast::flat_buffer _buffer{ 8192 };				// 用于存储从客户端读取的数据
	boost::beast::http::request<boost::beast::http::dynamic_body> _request;		// HTTP请求对象，用于解析和处理请求
	boost::beast::http::response<boost::beast::http::dynamic_body> _response;	// HTTP响应对象，用于构建和发送响应
	boost::asio::steady_timer _deadline{
		_socket.get_executor(), std::chrono::seconds(10)					    	// 定时器，用于检查连接是否超时
	};

	std::string _get_url;
	std::unordered_map<std::string, std::string> _get_params;
};

