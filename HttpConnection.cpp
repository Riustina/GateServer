// HttpConnection.cpp

#include "HttpConnection.h"
#include "LogicSystem.h"
#include <iostream>

HttpConnection::HttpConnection(boost::asio::io_context& ioc) : _socket(ioc) {

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

unsigned char ToHex(unsigned char x)
{
	return  x > 9 ? x + 55 : x + 48;
}

unsigned char FromHex(unsigned char x)
{
	unsigned char y;
	if (x >= 'A' && x <= 'Z') y = x - 'A' + 10;
	else if (x >= 'a' && x <= 'z') y = x - 'a' + 10;
	else if (x >= '0' && x <= '9') y = x - '0';
	else assert(0);
	return y;
}

std::string UrlEncode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//判断是否仅有数字和字母构成
		if (isalnum((unsigned char)str[i]) ||
			(str[i] == '-') ||
			(str[i] == '_') ||
			(str[i] == '.') ||
			(str[i] == '~'))
			strTemp += str[i];
		else if (str[i] == ' ') //为空字符
			strTemp += "+";
		else
		{
			//其他字符需要提前加%并且高四位和低四位分别转为16进制
			strTemp += '%';
			strTemp += ToHex((unsigned char)str[i] >> 4);
			strTemp += ToHex((unsigned char)str[i] & 0x0F);
		}
	}
	return strTemp;
}

std::string UrlDecode(const std::string& str)
{
	std::string strTemp = "";
	size_t length = str.length();
	for (size_t i = 0; i < length; i++)
	{
		//还原+为空
		if (str[i] == '+') strTemp += ' ';
		//遇到%将后面的两个字符从16进制转为char再拼接
		else if (str[i] == '%')
		{
			assert(i + 2 < length);
			unsigned char high = FromHex((unsigned char)str[++i]);
			unsigned char low = FromHex((unsigned char)str[++i]);
			strTemp += high * 16 + low;
		}
		else strTemp += str[i];
	}
	return strTemp;
}

void HttpConnection::PreParseGetParam() {
	// 提取 URI  
	auto uri = _request.target();
	// 查找查询字符串的开始位置（即 '?' 的位置）  
	auto query_pos = uri.find('?');
	if (query_pos == std::string::npos) {
		_get_url = uri;
		return;
	}

	_get_url = uri.substr(0, query_pos);
	std::string query_string = uri.substr(query_pos + 1);
	std::string key;
	std::string value;
	size_t pos = 0;
	while ((pos = query_string.find('&')) != std::string::npos) {
		auto pair = query_string.substr(0, pos);
		size_t eq_pos = pair.find('=');
		if (eq_pos != std::string::npos) {
			key = UrlDecode(pair.substr(0, eq_pos)); // 假设有 url_decode 函数来处理URL解码  
			value = UrlDecode(pair.substr(eq_pos + 1));
			_get_params[key] = value;
		}
		query_string.erase(0, pos + 1);
	}
	// 处理最后一个参数对（如果没有 & 分隔符）  
	if (!query_string.empty()) {
		size_t eq_pos = query_string.find('=');
		if (eq_pos != std::string::npos) {
			key = UrlDecode(query_string.substr(0, eq_pos));
			value = UrlDecode(query_string.substr(eq_pos + 1));
			_get_params[key] = value;
		}
	}
}

void HttpConnection::HandleRequest() {
	// 根据请求的内容生成相应的响应
	_response.version(_request.version());		// 设置响应的HTTP版本与请求相同
	_response.keep_alive(false);				// 设置连接不保活

	// 如果请求方法是GET
	if (_request.method() == boost::beast::http::verb::get) {
		PreParseGetParam();
		bool success = LogicSystem::getInstance().HandleGet(_get_url, shared_from_this());	// 处理GET请求，生成响应
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

	// 如果请求方法是POST
	if (_request.method() == boost::beast::http::verb::post) {
		bool success = LogicSystem::getInstance().HandlePost(_request.target(), shared_from_this());	// 处理GET请求，生成响应
		if (!success) {
			// 处理POST请求失败，生成404 Not Found响应
			_response.result(boost::beast::http::status::not_found);
			_response.set(boost::beast::http::field::content_type, "text/plain");
			boost::beast::ostream(_response.body()) << "The resource '" << _request.target() << "' was not found.";
			WriteResponse();	// 发送响应
			return;
		}

		// 处理POST请求成功，生成200 OK响应
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


