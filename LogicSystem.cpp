#include "LogicSystem.h"
#include "HttpConnection.h"
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>

LogicSystem::LogicSystem() {
	// 构造函数，初始化成员变量
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		// 处理GET请求的Lambda函数，向客户端发送一个简单的响应
		boost::beast::ostream(connection->_response.body()) << "This is a test response for GET /get_test.";	// 设置响应体内容
	});
}

void LogicSystem::RegGet(const std::string& target, HttpHandler handler)
{
	// 注册GET请求处理函数，将目标路径和处理函数存储在一个映射中
	_getHandlers[target] = handler;
}

bool LogicSystem::HandleGet(const std::string& target, std::shared_ptr<HttpConnection> connection)
{
	// 处理GET请求，根据目标路径查找对应的处理函数并调用它
	auto it = _getHandlers.find(target);
	if (it != _getHandlers.end()) {
		it->second(connection);	// 调用处理函数，传入连接对象
		return true;			// 成功处理请求
	}
	return false;				// 没有找到对应的处理函数，处理失败
}