#pragma once
#include <functional>
#include <memory>
#include <iostream>
#include <map>
#include "Singleton.h"

class HttpConnection;	// 前向声明HttpConnection类，避免循环依赖
typedef std::function<void(std::shared_ptr<HttpConnection>)> HttpHandler;	// 定义一个函数类型，用于处理HTTP请求

class LogicSystem : public Singleton<LogicSystem> {
	friend class Singleton<LogicSystem>;	// 让Singleton<LogicSystem>成为LogicSystem的友元类，可以访问其私有成员
public :
	~LogicSystem() = default;
	bool HandleGet(const std::string& target, std::shared_ptr<HttpConnection> connection);	// 处理GET请求的函数，返回是否成功处理请求
	bool HandlePost(const std::string& target, std::shared_ptr<HttpConnection> connection); // 处理POST请求的函数，返回是否成功处理请求
	void RegGet(const std::string& target, HttpHandler);		// 注册GET请求处理函数的函数
	void RegPost(const std::string& target, HttpHandler handler);		// 注册POST请求处理函数的函数
private:
	LogicSystem();
	std::map<std::string, HttpHandler> _getHandlers;	// 存储GET请求处理函数的映射，键是目标路径，值是处理函数
	std::map<std::string, HttpHandler> _postHandlers;	// 存储POST请求处理函数的映射，键是目标路径，值是处理函数
};

