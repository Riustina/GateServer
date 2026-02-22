#include "CServer.h"
#include <iostream>
#include "HttpConnection.h"

CServer::CServer(boost::asio::io_context& ioc, unsigned short& port) : 
	_ioc(ioc), 
	_acceptor(ioc, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
	_socket(ioc)
{

}

void CServer::Start()
{
	auto self = shared_from_this();
	_acceptor.async_accept(_socket, [this, self](boost::beast::error_code ec)
	{
		try {
			// 出错就放弃连接，继续监听其他链接
			if (ec) {
				self->Start();
				return;
			}
			// 成功接受连接后，创建一个新的HttpConnection对象来处理这个连接，并启动它
			std::make_shared<HttpConnection>(std::move(self->_socket))->Start();	// socket被HttpConnection对象接管，不能再使用了，所以要std::move

		}
		catch (std::exception& e)
		{
			std::cerr << "[CServer.cpp] 函数 [Start()] Exception: " << e.what() << std::endl;
		}
	});
}