#include "HttpConnection.h"
#include "LogicSystem.h"
#include <iostream>

HttpConnection::HttpConnection(boost::asio::ip::tcp::socket socket) : _socket(std::move(socket)) {

}

void HttpConnection::Start() {
	auto self = shared_from_this();	// 获取shared_ptr对象的副本，确保在异步操作期间对象不会被销毁
	// 异步读取HTTP请求
	boost::beast::http::async_read(_socket, _buffer, _request,
		[self](boost::beast::error_code ec, std::size_t bytes_transferred)
		{
			try {
				if (ec) {
					std::cerr << "[HttpConnection.cpp] 函数 [Start()] async_read Error: " << ec.message() << std::endl;
					return;	// 读取失败，放弃连接
				}
				// 处理请求并生成响应
				boost::ignore_unused(bytes_transferred);	// 避免未使用参数的编译警告
				self->HandleRequest();	// 处理请求
				self->CheckDeadLine();	// 检查连接是否超时
			}
			catch (std::exception& e)
			{
				std::cerr << "[HttpConnection.cpp] 函数 [Start()] Exception: " << e.what() << std::endl;
			}
		});
}

void HttpConnection::HandleRequest() {
	// 根据请求的内容生成相应的响应
	_response.version(_request.version());		// 设置响应的HTTP版本与请求相同
	_response.keep_alive(false);				// 设置连接不保活
	if (_request.method() == boost::beast::http::verb::get) {
		// 如果请求方法是GET
		bool success = LogicSystem::getInstance().HandleGet(_request.target(), shared_from_this());	// 处理GET请求，生成响应
		if (!success) {
			// 处理GET请求失败，生成404 Not Found响应
			_response.result(boost::beast::http::status::not_found);
			_response.set(boost::beast::http::field::content_type, "text/plain");
			boost::beast::ostream(_response.body()) << "The resource '" << _request.target() << "' was not found.";
			WriteResponse();	// 发送响应
			return;
		}

		// 处理GET请求成功，生成200 OK响应
		_response.result(boost::beast::http::status::ok);
		_response.set(boost::beast::http::field::server, "GateServer");
		_response.set(boost::beast::http::field::content_type, "text/plain");
		// boost::beast::ostream(_response.body()) << "The resource '" << _request.target() << "' was found.";
		WriteResponse();	// 发送响应
		return;
	}



}

void HttpConnection::WriteResponse() {
	auto self = shared_from_this();	// 获取shared_ptr对象的副本，确保在异步操作期间对象不会被销毁
	_response.content_length(_response.body().size());	// 设置响应的Content-Length头部字段，表示响应体的大小
	// 异步写入HTTP响应
	boost::beast::http::async_write(_socket, _response,
		[self](boost::beast::error_code ec, std::size_t bytes_transferred)
		{
			try {
				if (ec) {
					std::cerr << "[HttpConnection.cpp] 函数 [WriteResponse()] async_write Error: " << ec.message() << std::endl;
					return;	// 写入失败，放弃连接
				}
				boost::ignore_unused(bytes_transferred);	// 避免未使用参数的编译警告
				// 写入成功，关闭连接
				self->_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_send);	// 关闭发送端，表示响应已经发送完毕
				self->_deadline.cancel();	// 取消定时器
			}
			catch (std::exception& e)
			{
				std::cerr << "[HttpConnection.cpp] 函数 [WriteResponse()] Exception: " << e.what() << std::endl;
			}
		});
}

void HttpConnection::CheckDeadLine() {
	auto self = shared_from_this();	// 获取shared_ptr对象的副本，确保在异步操作期间对象不会被销毁
	_deadline.async_wait([self](boost::beast::error_code ec) {
		try {
			if (ec) {
				if (ec != boost::asio::error::operation_aborted) {
					std::cerr << "[HttpConnection.cpp] 函数 [CheckDeadLine()] async_wait Error: " << ec.message() << std::endl;
				}
				return;	// 定时器被取消，放弃检查
			}
			// 定时器到期，关闭连接
			self->_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both);	// 关闭连接的发送和接收端
		}
		catch (std::exception& e)
		{
			std::cerr << "[HttpConnection.cpp] 函数 [CheckDeadLine()] Exception: " << e.what() << std::endl;
		}
	});
}


