// LogicSystem.cpp

#include "LogicSystem.h"
#include "HttpConnection.h"
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "global.h"

LogicSystem::LogicSystem() {
	// 构造函数，初始化成员变量
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		// 处理GET请求的Lambda函数，向客户端发送一个简单的响应
		boost::beast::ostream(connection->_response.body()) << "This is a test response for GET /get_test.";	// 设置响应体内容
		for (auto& param : connection->_get_params) {
			boost::beast::ostream(connection->_response.body()) << "\nParam: " << param.first << " = " << param.second;	// 输出GET请求的参数
		}
	});
	
	RegPost("/get_verifycode", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());	// 获取POST请求的请求体内容
		std::cerr << "Received POST /get_verifycode with body: " << body_str << std::endl;	// 输出请求体内容
		connection->_response.set(boost::beast::http::field::content_type, "text/json");	// 设置响应的Content-Type头部字段

		Json::Reader reader;		// 创建一个Json::Reader对象，用于解析JSON数据
		Json::Value source;			// 创建一个Json::Value对象，用于存储解析后的JSON数据
		Json::Value response;		// 创建一个Json::Value对象，用于构建响应数据
		std::string jsonStr;		// 定义一个字符串变量，用于存储响应数据转换成的JSON字符串

		// 解析请求体中的JSON数据，并检查是否包含email字段
		bool parseSuccess = reader.parse(body_str, source);
		// 如果解析失败或者缺少email字段，输出错误信息并返回错误响应
		if (!parseSuccess || !source.isMember("email")) {
			if (!parseSuccess) {
				std::cerr << "[LogicSystem.cpp] 函数 [RegPost] Failed to parse JSON: " << reader.getFormattedErrorMessages() << std::endl;	// 输出解析错误信息
			}
			else {
				std::cerr << "[LogicSystem.cpp] 函数 [RegPost] JSON does not contain 'email' field." << std::endl;	// 输出缺少email字段的错误信息
			}
			response["error"] = ErrorCodes::Error_Json;	// 设置错误码
			jsonStr = response.toStyledString();	// 将响应数据转换为JSON字符串
			boost::beast::ostream(connection->_response.body()) << jsonStr;	// 设置响应体内容为JSON字符串
			return true;
		}

		// 解析成功且包含email字段，输出email值并构建成功响应
		auto email = source["email"].asString();	// 从请求体中获取email字段的值
		// std::cerr << "Email: " << email << std::endl;	// 输出email值
		response["error"] = ErrorCodes::Success;	// 设置成功的错误码
		response["email"] = source["email"];	// 将请求中的email字段原样返回到响应中
		jsonStr = response.toStyledString();	// 将响应数据转换为JSON字符串
		boost::beast::ostream(connection->_response.body()) << jsonStr;	// 设置响应体内容为JSON字符串

		return true;
	});
}

void LogicSystem::RegGet(const std::string& target, HttpHandler handler)
{
	// 注册GET请求处理函数，将目标路径和处理函数存储在一个映射中
	_getHandlers[target] = handler;
}

void LogicSystem::RegPost(const std::string& target, HttpHandler handler)
{
	// 注册GET请求处理函数，将目标路径和处理函数存储在一个映射中
	_postHandlers[target] = handler;
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

bool LogicSystem::HandlePost(const std::string& target, std::shared_ptr<HttpConnection> connection)
{
	// 处理GET请求，根据目标路径查找对应的处理函数并调用它
	auto it = _postHandlers.find(target);
	if (it != _postHandlers.end()) {
		it->second(connection);	// 调用处理函数，传入连接对象
		return true;			// 成功处理请求
	}
	return false;				// 没有找到对应的处理函数，处理失败
}