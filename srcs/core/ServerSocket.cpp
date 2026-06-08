#include "ServerSocket.hpp"

// 역할: 멤버 fd 만 무효값(-1)으로 두는 가벼운 초기화.
// 책임: 실제 리소스 획득은 init() 에 위임. 객체 수명 동안 잘못된 close 호출이
//       발생하지 않도록 -1 상태를 보장하는 것만 담당.
ServerSocket::ServerSocket() : _server_fd(-1)
{
}

// 역할: 지정 host:port 에 TCP 리스닝 소켓을 띄우고 논블로킹 상태로 만들며,
//       이 엔드포인트에 라우팅될 ServerBlock 목록을 보관.
// 책임: 소켓 생성/SO_REUSEADDR/bind/listen/F_SETFL 흐름을 한 번에 묶어 ServerManager
//       가 epoll 에 등록만 하면 되는 상태까지 준비. 실패는 예외로 호출자에게 보고하고,
//       부분 성공 시점에 잡힌 fd 는 throw 직전 close 로 누수 방지.
//       서버블록 매칭 로직(Host 헤더 기반)은 ClientSocket 책임이라 여기선 보관만 함.
void ServerSocket::init(std::string host, int port, const std::vector<const ServerBlock*>& serverBlocks)
{
	int flags;

	this->_server_blocks = serverBlocks;

	struct addrinfo hints, *res;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo(host.c_str(), NULL, &hints, &res) != 0 || !res)
		throw std::runtime_error("ServerSocket: getaddrinfo() failed: " + host);
	memset(&_address, 0, sizeof(_address));
	_address.sin_family = AF_INET;
	_address.sin_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr;
	_address.sin_port = htons(port);
	freeaddrinfo(res);

	if ((this->_server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		throw std::runtime_error("ServerSocket: socket() failed");
	}

	int opt = 1;
	if (setsockopt(this->_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
		throw std::runtime_error("setsockopt() failed");
	}

	if (bind(this->_server_fd, reinterpret_cast<struct sockaddr*>(&_address), sizeof(_address)) < 0){
		throw std::runtime_error("bind() failed");
	}

	flags = fcntl(_server_fd, F_GETFL, 0);
	if (flags < 0){
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

	std::cout << "[server] listening " << host << ":" << port
			  << " (fd=" << _server_fd << ")" << std::endl;
}

// 역할: 보유 중인 listen fd 가 유효하면 close.
// 책임: ServerManager 가 ServerSocket 컨테이너를 파괴할 때 자동으로 호출되어
//       프로세스 종료 직전까지 살아있는 리스닝 fd 의 안전한 회수를 보장.
ServerSocket::~ServerSocket()
{
	if (this->_server_fd >= 0)
		close(_server_fd);
}
