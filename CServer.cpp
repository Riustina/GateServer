// CServer.cpp

#include "CServer.h"
#include <iostream>
#include "HttpConnection.h"
#include "AsioIOServicePool.h"

CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) : 
	_ioc(ioc), 
	_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port))
{

}

// CServer.cpp
void CServer::Start()
{
    auto self = shared_from_this();
	auto& io_context = AsioIOServicePool::getInstance().GetIOService();
	std::shared_ptr<HttpConnection> new_connection = std::make_shared<HttpConnection>(io_context);  // 创建一个新的HttpConnection对象，使用从AsioIOServicePool获取的io_context
    _acceptor.async_accept(new_connection->GetSocket(), [self, new_connection](boost::beast::error_code ec)
        {
            try {
                if (!ec) {
                    // 成功接受连接，创建HttpConnection处理
                    new_connection->Start();
                }
                else {
                    std::cerr << "[CServer.cpp] 函数 [Start()] Accept error: " << ec.message() << std::endl;
                }

                // 无论成功失败，都重新开始监听新连接
                self->Start();
            }
            catch (std::exception& e) {
                std::cerr << "[CServer.cpp] 函数 [Start()] Exception: " << e.what() << std::endl;
                // 异常后也尝试重新监听
                self->Start();
            }
        });
}