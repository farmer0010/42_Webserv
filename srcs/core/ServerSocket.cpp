#include "ServerSocket.hpp"

ServerSocket::ServerSocket()
{
	int flags;

	if (this->_server_fd = socket(AF_INET, SOCK_STREAM, 0) < 0){
		throw std::runtime_error("Socket creation failed");
	}

	memset((void*)&_address, 0, sizeof(_address));
	this->_address.sin_family = AF_INET;
	this->_address.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0 (모든 인터페이스에서 접속 허용)
	this->_address.sin_port = htons(PORT);

	if (bind(this->_server_fd, (struct sockaddr*)&_address, sizeof(_address)) < 0){
		close(_server_fd);
		throw std::runtime_error("bind() failed");
	}

	flags = fcntl(_server_fd, F_GETFL, 0);
	if (flags < 0){
		close(_server_fd);
		throw std::runtime_error("fcntl()/read_curr flag failed");
	}

	if (fcntl(_server_fd, F_SETFL, flags | O_NONBLOCK) == -1){
		close(_server_fd);
		throw std::runtime_error("fcntl()/change NonBlocking failed");
	}

	if (listen(_server_fd, SOMAXCONN) < 0){
		close(_server_fd);
		throw std::runtime_error("listen() failed");
	}

	std::cout << "ServerSocket(" << "_server_fd:" << _server_fd << "): constructor called\n";
}

ServerSocket::~ServerSocket()
{
	if (this->_server_fd > 0)
		close(_server_fd);
	std::cout << "ServerSocket : destructor called\n";
}

int ServerSocket::getServerfd()
{
	return this->_server_fd;
}
