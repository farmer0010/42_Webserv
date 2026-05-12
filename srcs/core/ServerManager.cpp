#include "ServerManager.hpp"

ServerManager::ServerManager() : _epoll_fd(-1)
{
	std::cout << "ServerManager : constructor called\n";
}

ServerManager::~ServerManager()
{
	close(_epoll_fd);
	for (std::map<int, ServerSocket*>::iterator it = _servers.begin(); it != _servers.end(); ++it) {
		delete it->second;
	}
	for (std::map<int, ClientSocket*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
		delete it->second;
	}
	std::cout << "ServerManager : destructor called\n";
}

void ServerManager::init(const Config& config) {

	try {
		ServerSocket* server = new ServerSocket();
		_servers[server->getFd()] = server;
	} catch (const std::exception& e) {
		std::cerr << "ServerManager: Error initializing ServerManager: " << e.what() << std::endl;
		throw; // Rethrow the exception after logging
	}

	// Initialize epoll
	_epoll_fd = epoll_create(10);
	if (_epoll_fd < 0){
		close(_epoll_fd);
		throw std::runtime_error("epoll_create() failed\n");
	}

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = _servers.begin()->first;
	if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0){
		close(_epoll_fd);
		delete _servers[event.data.fd];
		throw std::runtime_error("epoll_ctl() failed\n");
	}
}

void ServerManager::run() {
    struct epoll_event events[MAX_EVENTS];

    std::cout << "Map-based Echo Server is running..." << std::endl;

    while (true) {
        int event_count = epoll_wait(_epoll_fd, events, MAX_EVENTS, -1);
        if (event_count < 0) {
            std::cerr << "epoll_wait() failed: " << strerror(errno) << std::endl;
            continue;
        }

        for (int i = 0; i < event_count; ++i) {
            int active_fd = events[i].data.fd;
            // 새 요청 처리 (서버 소켓에서 이벤트 발생)
            if (_servers.find(active_fd) != _servers.end()) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int new_client_fd = accept(active_fd, (struct sockaddr*)&client_addr, &client_len);
                if (new_client_fd < 0) continue;

                int flags = fcntl(new_client_fd, F_GETFL, 0);
                fcntl(new_client_fd, F_SETFL, flags | O_NONBLOCK);

                struct epoll_event event;
                event.events = EPOLLIN;
                event.data.fd = new_client_fd;
                epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, new_client_fd, &event);

                _clients[new_client_fd] = new ClientSocket(new_client_fd, client_addr);

                std::cout << "New client connected! FD: " << new_client_fd << std::endl;
            }
            // 클라이언트 소켓에서 이벤트 발생 (데이터 수신 또는 연결 종료)
            else if (_clients.find(active_fd) != _clients.end()) {
                char buffer[1024];
                std::memset(buffer, 0, sizeof(buffer));

                int bytes_read = recv(active_fd, buffer, sizeof(buffer), 0);

                if (bytes_read > 0) {
                    ClientSocket* client = _clients[active_fd];
                    client->getRecvBuffer().insert(
                        client->getRecvBuffer().end(),
                        buffer,
                        buffer + bytes_read
                    );
                }
                // 클라이언트가 연결을 종료한 경우
                else if (bytes_read == 0) {
                    std::cout << "Client disconnected. FD: " << active_fd << std::endl;
                    epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, active_fd, 0); // 레이더망에서 제거
                    delete _clients[active_fd]; // ClientSocket 객체 삭제
                    _clients.erase(active_fd); // 맵에서 제거
                }
                //에러처리 없음
                /*
                왜냐면 recv()가 -1을 반환하는 경우는 EAGAIN 또는 EWOULDBLOCK일 때만 발생할 수 있기 때문.
                이 경우는 단순히 아직 읽을 데이터가 없다는 것을 의미하므로,
                에러로 간주하지 않고 무시해도 됩니다. 따라서 recv()가 -1을 반환하더라도, 이는 정상적인 상황일 수 있으므로,
                에러 처리를 하지 않고 계속해서 이벤트 루프를 진행하는 것이 적절합니다
                */
            }
        }
    }
}
