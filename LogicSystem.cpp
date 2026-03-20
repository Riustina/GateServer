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
#include "StatusGrpcClient.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <sstream>
#include <zlib.h>

namespace {
int DecodeBase64Char(unsigned char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

bool DecodeBase64(const std::string& input, std::string& output)
{
	output.clear();
	int value = 0;
	int bit_count = -8;
	for (unsigned char c : input) {
		if (std::isspace(c)) {
			continue;
		}
		if (c == '=') {
			break;
		}
		const int decoded = DecodeBase64Char(c);
		if (decoded < 0) {
			return false;
		}
		value = (value << 6) + decoded;
		bit_count += 6;
		if (bit_count >= 0) {
			output.push_back(static_cast<char>((value >> bit_count) & 0xFF));
			bit_count -= 8;
		}
	}
	return !output.empty();
}

bool DecodeIncomingImagePayload(const std::string& base64_content,
	const std::string& content_encoding,
	std::string& output_bytes,
	std::string& extension)
{
	output_bytes.clear();
	extension = ".png";

	std::string decoded;
	if (!DecodeBase64(base64_content, decoded)) {
		return false;
	}

	if (content_encoding == "zlib+png") {
		if (decoded.size() < 4) {
			return false;
		}
		const unsigned char* header = reinterpret_cast<const unsigned char*>(decoded.data());
		const uLongf expected_size =
			(static_cast<uLongf>(header[0]) << 24) |
			(static_cast<uLongf>(header[1]) << 16) |
			(static_cast<uLongf>(header[2]) << 8) |
			static_cast<uLongf>(header[3]);
		if (expected_size == 0) {
			return false;
		}

		std::string restored(expected_size, '\0');
		uLongf dest_len = expected_size;
		const int zlib_result = ::uncompress(
			reinterpret_cast<Bytef*>(&restored[0]),
			&dest_len,
			reinterpret_cast<const Bytef*>(decoded.data() + 4),
			static_cast<uLong>(decoded.size() - 4));
		if (zlib_result != Z_OK) {
			return false;
		}
		restored.resize(dest_len);
		output_bytes = std::move(restored);
		extension = ".png";
		return true;
	}

	output_bytes = std::move(decoded);
	if (output_bytes.size() >= 8 &&
		static_cast<unsigned char>(output_bytes[0]) == 0x89 &&
		output_bytes[1] == 'P' && output_bytes[2] == 'N' && output_bytes[3] == 'G') {
		extension = ".png";
	}
	else {
		extension = ".jpg";
	}
	return true;
}

bool SaveUploadedImage(const std::string& upload_id,
	const std::string& base64_content,
	const std::string& content_encoding,
	std::string& saved_path)
{
	saved_path.clear();
	std::string bytes;
	std::string extension;
	if (!DecodeIncomingImagePayload(base64_content, content_encoding, bytes, extension)) {
		return false;
	}

	const boost::filesystem::path upload_root =
		boost::filesystem::absolute(boost::filesystem::current_path() / "uploads" / "chat_images");
	boost::filesystem::create_directories(upload_root);

	const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::system_clock::now().time_since_epoch()).count();
	std::ostringstream oss;
	oss << upload_id << "_" << now_ms << extension;
	const boost::filesystem::path image_path = upload_root / oss.str();

	std::ofstream output(image_path.string(), std::ios::binary);
	if (!output.is_open()) {
		return false;
	}
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	output.close();
	if (!output.good()) {
		return false;
	}

	saved_path = std::string("chat_images/") + image_path.filename().string();
	return true;
}

bool ResolveImageResourcePath(const std::string& resource_key,
	boost::filesystem::path& image_path,
	std::string& content_type)
{
	image_path.clear();
	content_type = "application/octet-stream";
	if (resource_key.empty()) {
		return false;
	}

	std::string normalized = resource_key;
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	if (normalized.find("..") != std::string::npos) {
		return false;
	}

	static const std::string kPrefix = "chat_images/";
	if (normalized.rfind(kPrefix, 0) != 0 || normalized.size() <= kPrefix.size()) {
		return false;
	}

	const std::string file_name = normalized.substr(kPrefix.size());
	if (file_name.find('/') != std::string::npos || file_name.find('\\') != std::string::npos) {
		return false;
	}

	const boost::filesystem::path upload_root =
		boost::filesystem::absolute(boost::filesystem::current_path() / "uploads" / "chat_images");
	image_path = upload_root / file_name;
	if (!boost::filesystem::exists(image_path) || !boost::filesystem::is_regular_file(image_path)) {
		return false;
	}

	const std::string extension = image_path.extension().string();
	if (extension == ".png") {
		content_type = "image/png";
	}
	else if (extension == ".jpg" || extension == ".jpeg") {
		content_type = "image/jpeg";
	}
	else if (extension == ".bmp") {
		content_type = "image/bmp";
	}
	else if (extension == ".webp") {
		content_type = "image/webp";
	}
	else if (extension == ".gif") {
		content_type = "image/gif";
	}
	return true;
}
}

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
		connection->_response.set(boost::beast::http::field::content_type, "application/json; charset=utf-8");

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
			RedisManager::getInstance().Del("code_" + email);
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

	RegPost("/upload_image", [](std::shared_ptr<HttpConnection> connection) {
		auto body_str = boost::beast::buffers_to_string(connection->_request.body().data());
		std::cout << "[LogicSystem.cpp] [/upload_image] Received POST" << std::endl;
		connection->_response.set(boost::beast::http::field::content_type, "application/json; charset=utf-8");

		Json::Value root;
		Json::Reader reader;
		Json::Value src_root;

		auto send_error = [&](const std::string& message) {
			root["error"] = ErrorCodes::Error_Json;
			root["message"] = message;
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
			};

		if (!reader.parse(body_str, src_root)) {
			return send_error("JSON 解析失败");
		}

		for (const auto& field : { "upload_id", "content", "content_encoding" }) {
			if (!src_root.isMember(field)) {
				return send_error(std::string("缺少字段: ") + field);
			}
		}

		const std::string upload_id = src_root["upload_id"].asString();
		const std::string content = src_root["content"].asString();
		const std::string content_encoding = src_root["content_encoding"].asString();
		if (upload_id.empty() || content.empty()) {
			return send_error("上传参数无效");
		}

		std::string saved_path;
		if (!SaveUploadedImage(upload_id, content, content_encoding, saved_path)) {
			root["error"] = ErrorCodes::Error_Json;
			root["message"] = "图片上传失败，请尝试更小的图片";
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
		}

		root["error"] = Success;
		root["upload_id"] = upload_id;
		root["resource_key"] = saved_path;
		root["path"] = saved_path;
		boost::beast::ostream(connection->_response.body()) << root.toStyledString();
		return true;
		});

	RegGet("/download_image", [](std::shared_ptr<HttpConnection> connection) {
		connection->_response.set(boost::beast::http::field::server, "GateServer");

		const auto it = connection->_get_params.find("path");
		if (it == connection->_get_params.end()) {
			connection->_response.result(boost::beast::http::status::bad_request);
			connection->_response.set(boost::beast::http::field::content_type, "text/plain; charset=utf-8");
			boost::beast::ostream(connection->_response.body()) << "missing path";
			return true;
		}

		boost::filesystem::path image_path;
		std::string content_type;
		if (!ResolveImageResourcePath(it->second, image_path, content_type)) {
			connection->_response.result(boost::beast::http::status::not_found);
			connection->_response.set(boost::beast::http::field::content_type, "text/plain; charset=utf-8");
			boost::beast::ostream(connection->_response.body()) << "image not found";
			return true;
		}

		std::ifstream input(image_path.string(), std::ios::binary);
		if (!input.is_open()) {
			connection->_response.result(boost::beast::http::status::internal_server_error);
			connection->_response.set(boost::beast::http::field::content_type, "text/plain; charset=utf-8");
			boost::beast::ostream(connection->_response.body()) << "open image failed";
			return true;
		}

		std::ostringstream oss;
		oss << input.rdbuf();
		connection->_response.result(boost::beast::http::status::ok);
		connection->_response.set(boost::beast::http::field::content_type, content_type);
		boost::beast::ostream(connection->_response.body()) << oss.str();
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

		auto send_error = [&](ErrorCodes code, const std::string& message = "") {
			root["error"] = code;
			if (!message.empty()) {
				root["message"] = message;
			}
			boost::beast::ostream(connection->_response.body()) << root.toStyledString();
			return true;
			};

		// 1. 解析 JSON
		if (!reader.parse(body_str, src_root)) {
			std::cerr << "[LogicSystem.cpp] [/user_login] JSON 解析失败" << std::endl;
			return send_error(ErrorCodes::Error_Json);
		}

		// 2. 校验必填字段
		for (const auto& field : { "email", "passwd" }) {
			if (!src_root.isMember(field)) {
				std::cerr << "[LogicSystem.cpp] [/user_login] 缺少字段: " << field << std::endl;
				return send_error(ErrorCodes::Error_Json);
			}
		}

		const std::string email = src_root["email"].asString();
		const std::string passwd = src_root["passwd"].asString();

		// 3. 校验用户名与密码
		UserInfo userInfo;
		if (MySqlMgr::getInstance().CheckLogin(email, passwd, userInfo) != 0) {
			std::cout << "[LogicSystem.cpp] [/user_login] 密码错误，email: " << email << std::endl;
			return send_error(ErrorCodes::PasswdInvalid);
		}

		// 4. 通过 gRPC 向 StatusServer 申请分配 ChatServer
		auto reply = StatusGrpcClient::getInstance().GetChatServer(userInfo.uid);
		if (reply.error()) {
			std::cerr << "[LogicSystem.cpp] [/user_login] gRPC 分配 ChatServer 失败，error: " << reply.error()
				<< "，uid: " << userInfo.uid << std::endl;
			return send_error(ErrorCodes::RPC_Failed, u8"当前没有可用的聊天服务器，请稍后再试");
		}

		// 5. 返回成功，携带分配到的 ChatServer 信息供客户端直连
		std::cout << "[LogicSystem.cpp] [/user_login] 登录成功，uid: " << userInfo.uid
			<< "，分配 ChatServer host: " << reply.host() << std::endl;
		root["error"] = 0;
		root["uid"] = userInfo.uid;
		root["email"] = email;
		root["token"] = reply.token();
		root["host"] = reply.host();
		root["port"] = reply.port();
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
