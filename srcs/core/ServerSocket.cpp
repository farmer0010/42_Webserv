#include "ServerSocket.hpp"

ServerSocket::ServerSocket()
{
	std::cout << "ServerSocket(" << "_server_fd:" << _server_fd << "): constructor called\n";
}

void ServerSocket::init(int port,const std::vector<ServerBlock>& serverBlocks)
{
	int flags;

	// 멤버변수 초기화
	this->_server_blocks = serverBlocks;
	memset((void*)&_address, 0, sizeof(_address));
	this->_address.sin_family = AF_INET;
	this->_address.sin_addr.s_addr = INADDR_ANY;
	this->_address.sin_port = htons(port);

	// 소켓 생성
	if ((this->_server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		throw std::runtime_error("ServerSocket: socket() failed");
	}

	// 소켓 옵션 설정 (재사용 가능)
	int opt = 1;
	if (setsockopt(this->_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
		throw std::runtime_error("setsockopt() failed");
	}

	// 소켓 바인딩
	if (bind(this->_server_fd, reinterpret_cast<struct sockaddr*>(&_address), sizeof(_address)) < 0){
		throw std::runtime_error("bind() failed");
	}

	// 기존 소켓 플래그 가져오기
	flags = fcntl(_server_fd, F_GETFL, 0);
	if (flags < 0){
		throw std::runtime_error("fcntl()/read_curr flag failed");
	}

	// 소켓을 논블로킹 모드로 설정
	if (fcntl(_server_fd, F_SETFL, flags | O_NONBLOCK) == -1){
		close(_server_fd);
		throw std::runtime_error("fcntl()/change NonBlocking failed");
	}

	// 소켓을 수신 대기 상태로 설정
	if (listen(_server_fd, SOMAXCONN) < 0){
		close(_server_fd);
		throw std::runtime_error("listen() failed");
	}
}

ServerSocket::~ServerSocket()
{
	if (this->_server_fd > 0)
		close(_server_fd);
	std::cout << "ServerSocket : destructor called\n";
}

int ServerSocket::getFd()
{
	return this->_server_fd;
}
