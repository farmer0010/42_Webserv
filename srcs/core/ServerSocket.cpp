#include "ServerSocket.hpp"

ServerSocket::ServerSocket() : _server_fd(-1)
{
	std::cout << "ServerSocket: constructor called\n";
}

void ServerSocket::init(std::string host, int port, const std::vector<const ServerBlock*>& serverBlocks)
{
	int flags;

	// 서버 블록 저장 (포트+ip로 그룹화된 서버블록이 전달됨)
	this->_server_blocks = serverBlocks;

	// 소켓 주소 구조체 초기화
	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host.c_str(), NULL, &hints, &res) != 0)
		throw std::runtime_error("ServerSocket: getaddrinfo() failed: " + host);
	memset(&_address, 0, sizeof(_address));
	_address.sin_family = AF_INET;
	_address.sin_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr;
	_address.sin_port = htons(port);
	freeaddrinfo(res);

	// 소켓 생성
	if ((this->_server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		throw std::runtime_error("ServerSocket: socket() failed");
	}

	// 소켓 옵션 설정 (포트 재사용 설정)
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
	if (this->_server_fd >= 0)
		close(_server_fd);
	std::cout << "ServerSocket : destructor called\n";
}

