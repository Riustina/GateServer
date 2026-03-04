// LogicSystem.cpp

#include "LogicSystem.h"
#include "HttpConnection.h"
#include <boost/beast/http.hpp>
#include <boost/beast.hpp>
#include <json/json.h>
#include <json/value.h>
#include <json/reader.h>
#include "global.h"
#include "VerifyGrpcClient.h"
#include "RedisManager.h"
#include "MySqlMgr.h"

LogicSystem::LogicSystem() {
	// 构造函数，初始化成员变量
	RegGet("/get_test", [](std::shared_ptr<HttpConnection> connection) {
		// 处理GET请求的Lambda函数，向客户端发送一个简单的响应
		std::cout << "[LogicSystem] [/get_test] Received GET";
		boost::beast::ostream(connection->_response.body()) << "This is a test response for GET /get_test.";	// 设置响应体内容
		for (auto& param : connection->_get_params) {
			boost::beast::ostream(connection->_response.body()) << "\nParam: " << param.first << " = " << param.second;	// 输出GET请求的参数
		}
	});
	
	RegPost("/get_verifycode", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());	// 获取POST请求的请求体内容
		std::cout << "[LogicSystem] [/get_verifycode] Received POST with body: " << body_str << std::endl;	// 输出请求体内容
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
				std::cerr << "[LogicSystem.cpp] [/get_verifycode] Failed to parse JSON: " << reader.getFormattedErrorMessages() << std::endl;	// 输出解析错误信息
			}
			else {
				std::cerr << "[LogicSystem.cpp] [/get_verifycode] JSON does not contain 'email' field." << std::endl;	// 输出缺少email字段的错误信息
			}
			response["error"] = ErrorCodes::Error_Json;	// 设置错误码
			jsonStr = response.toStyledString();	// 将响应数据转换为JSON字符串
			boost::beast::ostream(connection->_response.body()) << jsonStr;	// 设置响应体内容为JSON字符串
			return true;
		}

		// 解析成功且包含email字段，输出email值并构建成功响应
		auto email = source["email"].asString();	// 从请求体中获取email字段的值
		GetVerifyRsp rsp = VerifyGrpcClient::getInstance().GetVerifyCode(email);	// 调用VerifyGrpcClient的GetVerifyCode方法，传入email参数

		response["error"] = rsp.error();	// 设置成功的错误码
		response["email"] = source["email"];	// 将请求中的email字段原样返回到响应中
		jsonStr = response.toStyledString();	// 将响应数据转换为JSON字符串
		boost::beast::ostream(connection->_response.body()) << jsonStr;	// 设置响应体内容为JSON字符串

		return true;
	});

	// 注册处理用户注册的POST请求的Lambda函数，处理逻辑包括解析请求体中的JSON数据、校验验证码、查询数据库等
	RegPost("/user_register", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "[LogicSystem.cpp] [/user_register] Received POST with body: " << body_str << std::endl;
		connection->_response.set(boost::beast::http::field::content_type, "text/json");

		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		// 辅助 lambda：写错误响应并返回
		auto send_error = [&](ErrorCodes code) {
			root["error"] = code;
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
			};

		// 1. 解析 JSON
		if (!reader.parse(body_str, src_root)) {
			std::cerr << "[LogicSystem.cpp] [/user_register] JSON 解析失败" << std::endl;
			return send_error(ErrorCodes::Error_Json);
		}

		// 2. 校验必填字段是否存在
		for (const auto& field : { "user", "email", "passwd", "verifycode" }) {
			if (!src_root.isMember(field)) {
				std::cerr << "[LogicSystem.cpp] [/user_register] 缺少字段: " << field << std::endl;
				return send_error(ErrorCodes::Error_Json);
			}
		}

		const std::string email = src_root["email"].asString();
		const std::string username = src_root["user"].asString();
		const std::string passwd = src_root["passwd"].asString();
		const std::string verifycode = src_root["verifycode"].asString();

		// 3. 从 Redis 取验证码
		std::string verify_code;
		if (!RedisManager::getInstance().Get("code_" + email, verify_code)) {
			std::cerr << "[user_register] [/user_register] 验证码已过期，email: " << email << std::endl;
			return send_error(ErrorCodes::VerifyExpired);
		}

		// 4. 校验验证码
		if (verify_code != verifycode) {
			std::cerr << "[user_register] 验证码错误，email: " << email << std::endl;
			return send_error(ErrorCodes::VerifyCodeError);
		}

		// 5. 查找数据库判断用户是否存在
		int uid = MySqlMgr::getInstance().RegUser(username, email, passwd);
		bool valid = (uid > 0);
		if (!valid) {  // 捕获所有错误情况
			// 根据不同错误码给出不同的错误信息
			switch (uid) {
			case 0:
				std::cout << "[LogicSystem.cpp] [/user_register] 用户名或邮箱已存在" << std::endl;
				return send_error(ErrorCodes::UserExists);
				break;
			case -1:
				std::cerr << "[LogicSystem.cpp] [/user_register] SQL异常" << std::endl;
				break;
			case -2:
				std::cerr << "[LogicSystem.cpp] [/user_register] 无法获取数据库连接" << std::endl;
				break;
			case -3:
				std::cerr << "[LogicSystem.cpp] [/user_register] 存储过程未返回结果" << std::endl;
				break;
			case -4:
				std::cerr << "[LogicSystem.cpp] [/user_register] 发生标准异常" << std::endl;
				break;
			case -5:
				std::cerr << "[LogicSystem.cpp] [/user_register] 发生未知异常" << std::endl;
				break;
			default:
				std::cerr << "[LogicSystem.cpp] [/user_register] 未预期的错误码: " << uid << std::endl;
				break;
			}
			return send_error(ErrorCodes::MySQLFailed);
		}

		// 6. 返回成功
		std::cout << "[LogicSystem.cpp] [/user_register] " << email << " / " << username << " 注册成功" << std::endl;
		RedisManager::getInstance().Del("code_" + email);	// 删除 Redis 中的验证码，避免重复使用
		root["error"] = 0;
		root["uid"] = uid;
		root["email"] = email;
		root["user"] = username;
		root["passwd"] = passwd;
		root["verifycode"] = verifycode;
		boost::beast::ostream(connection->_response.body()) << root.toStyledString();
		return true;
		});

	// 重置密码的POST请求处理函数，逻辑与用户注册类似，但需要校验用户名与邮箱的匹配关系，并更新数据库中的密码
	RegPost("/reset_pwd", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "[LogicSystem.cpp] [reset_pwd] Received POST with body: " << body_str << std::endl;
		connection->_response.set(boost::beast::http::field::content_type, "text/json");

		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		auto send_error = [&](ErrorCodes code) {
			root["error"] = code;
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
			};

		// 1. 解析 JSON
		if (!reader.parse(body_str, src_root)) {
			std::cerr << "[LogicSystem.cpp] [reset_pwd] JSON 解析失败" << std::endl;
			return send_error(ErrorCodes::Error_Json);
		}

		// 2. 校验必填字段
		for (const auto& field : { "user", "email", "passwd", "verifycode" }) {
			if (!src_root.isMember(field)) {
				std::cerr << "[LogicSystem.cpp] [reset_pwd] 缺少字段: " << field << std::endl;
				return send_error(ErrorCodes::Error_Json);
			}
		}

		const std::string email = src_root["email"].asString();
		const std::string username = src_root["user"].asString();
		const std::string passwd = src_root["passwd"].asString();
		const std::string verifycode = src_root["verifycode"].asString();

		// 3. 从 Redis 取验证码
		std::string verify_code;
		if (!RedisManager::getInstance().Get("code_" + email, verify_code)) {
			std::cout << "[LogicSystem.cpp] [reset_pwd] 验证码已过期，email: " << email << std::endl;
			return send_error(ErrorCodes::VerifyExpired);
		}

		// 4. 校验验证码
		if (verify_code != verifycode) {
			std::cout << "[LogicSystem.cpp] [reset_pwd] 验证码错误，email: " << email << std::endl;
			return send_error(ErrorCodes::VerifyCodeError);
		}

		// 5. 校验用户名与邮箱是否匹配（0是成功了）
		if (MySqlMgr::getInstance().CheckEmail(username, email) != 0) {
			std::cout << "[LogicSystem.cpp] [reset_pwd] 用户名与邮箱不匹配，user: " << username << std::endl;
			return send_error(ErrorCodes::EmailNotMatch);
		}

		// 6. 更新密码（0是成功了）
		if (MySqlMgr::getInstance().UpdatePwd(username, passwd) != 0) {
			std::cout << "[LogicSystem.cpp] [reset_pwd] 密码更新失败，user: " << username << std::endl;
			return send_error(ErrorCodes::PasswdUpFailed);
		}

		// 7. 返回成功
		std::cout << "[LogicSystem.cpp] [reset_pwd] 密码重置成功，user: " << username << std::endl;
		RedisManager::getInstance().Del("code_" + email);	// 删除 Redis 中的验证码，避免重复使用
		root["error"] = 0;
		root["email"] = email;
		root["user"] = username;
		root["passwd"] = passwd;
		root["verifycode"] = verifycode;
		boost::beast::ostream(connection->_response.body()) << root.toStyledString();
		return true;
		});

	// 用户登录的POST请求处理函数，逻辑包括解析请求体中的JSON数据、校验用户名与密码、通过gRPC向StatusServer申请分配ChatServer等
	RegPost("/user_login", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "[LogicSystem.cpp] [/user_login] Received POST with body: " << body_str << std::endl;
		connection->_response.set(boost::beast::http::field::content_type, "text/json");

		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		auto send_error = [&](ErrorCodes code) {
			root["error"] = code;
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
			};

		// 1. 解析 JSON
		if (!reader.parse(body_str, src_root)) {
			std::cerr << "[LogicSystem.cpp] [/user_login] JSON 解析失败" << std::endl;
			return send_error(ErrorCodes::Error_Json);
		}

		// 2. 校验必填字段
		for (const auto& field : { "user", "passwd" }) {
			if (!src_root.isMember(field)) {
				std::cerr << "[LogicSystem.cpp] [/user_login] 缺少字段: " << field << std::endl;
				return send_error(ErrorCodes::Error_Json);
			}
		}

		const std::string username = src_root["user"].asString();
		const std::string passwd = src_root["passwd"].asString();

		// 3. 校验用户名与密码
		UserInfo userInfo;
		if (MySqlMgr::getInstance().CheckLogin(username, passwd, userInfo) != 0) {
			std::cout << "[LogicSystem.cpp] [/user_login] 密码错误，user: " << username << std::endl;
			return send_error(ErrorCodes::PasswdInvalid);
		}

		// 4. 通过 gRPC 向 StatusServer 申请分配 ChatServer
		auto reply = StatusGrpcClient::GetInstance()->GetChatServer(userInfo.uid);
		if (reply.error()) {
			std::cerr << "[LogicSystem.cpp] [/user_login] gRPC 分配 ChatServer 失败，error: " << reply.error()
				<< "，uid: " << userInfo.uid << std::endl;
			return send_error(ErrorCodes::RPC_Failed);
		}

		// 5. 返回成功，携带分配到的 ChatServer 信息供客户端直连
		std::cout << "[LogicSystem.cpp] [/user_login] 登录成功，uid: " << userInfo.uid
			<< "，分配 ChatServer host: " << reply.host() << std::endl;
		root["error"] = 0;
		root["uid"] = userInfo.uid;
		root["user"] = username;
		root["token"] = reply.token();
		root["host"] = reply.host();
		boost::beast::ostream(connection->_response.body()) << root.toStyledString();
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