// GateServer.cpp : 定义控制台应用程序的入口点。

#include <iostream>
#include "CServer.h"
#include "ConfigManager.h"

int main()
{
	auto &configManager = ConfigManager::getInstance();	// 初始化配置管理器，加载配置文件
	unsigned short port = static_cast<unsigned short>(std::stoi(configManager["GateServer"]["port"]));	// 从配置文件中获取服务器监听的端口号


	try {
		boost::asio::io_context ioc{ 1 };	// 创建io_context对象，用于异步操作
		boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);	// 创建一个信号集，用于捕捉终止信号
		signals.async_wait([&ioc](boost::system::error_code /*ec*/, int /*signo*/)
		{
			ioc.stop();	// 当捕捉到终止信号时，停止io_context，结束服务器运行
		});
		std::make_shared<CServer>(ioc, port)->Start();	// 创建CServer对象并启动服务器
		std::cerr << "GateServer is running on port " << port << "...\n" << std::endl;
		ioc.run();						// 运行io_context，开始处理异步事件
	}
	catch (std::exception& e)
	{
		std::cerr << "[GateServer.cpp] 函数 [main()] Exception: " << e.what() << std::endl;
	}
	return 0;
}