// CServer.cpp

#include "CServer.h"
#include <iostream>
#include "HttpConnection.h"

CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) : 
	_ioc(ioc), 
	_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
	_socket(ioc)
{

}

// CServer.cpp
void CServer::Start()
{
    auto self = shared_from_this();
    _acceptor.async_accept(_socket, [this, self](boost::beast::error_code ec)
        {
            try {
                if (!ec) {
                    // 成功接受连接，创建HttpConnection处理
                    std::make_shared<HttpConnection>(std::move(_socket))->Start();
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