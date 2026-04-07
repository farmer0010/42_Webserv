#include "ServerManager.hpp"

ServerManager::ServerManager(int port)
{
	// Initialize server socket
	try {
		ServerSocket* server = new ServerSocket(port);
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
	event.data.fd = _servers.begin()->first; // Get the first server socket fd
	if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0){
		close(_epoll_fd);
		delete _servers[event.data.fd];
		throw std::runtime_error("epoll_ctl() failed\n");
	}

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

void ServerManager::run() {
    struct epoll_event events[MAX_EVENTS];

    std::cout << "Map-based Echo Server is running..." << std::endl;

    while (true) {
        int event_count = epoll_wait(_epoll_fd, events, MAX_EVENTS, -1);
        if (event_count < 0) {
			std::cerr << "epoll_wait() failed: " << strerror(errno) << std::endl;
			continue; // 에러 발생 시 루프 계속
		}

        for (int i = 0; i < event_count; ++i) {
            int active_fd = events[i].data.fd;

			/*
			@brief
			분기 A: active_fd가 _servers 맵에 존재한다면? (새 손님이 들어온 경우!)
				1. accept()로 손님 받기
				2. fcntl()로 논블로킹 설정
				3. epoll_ctl()로 레이더망에 등록 (정답: EPOLLIN!)
				4. ★ Map에 저장 ★ (ClientSocket 뼈대 객체 생성)
			*/
            if (_servers.find(active_fd) != _servers.end()) {
                struct sockaddr_in client_addr;
                socklen_t client_len = sizeof(client_addr);

                int new_client_fd = accept(active_fd, (struct sockaddr*)&client_addr, &client_len);
                if (new_client_fd < 0) continue;

                // 2. 논블로킹 설정 (fcntl)
                int flags = fcntl(new_client_fd, F_GETFL, 0);
                fcntl(new_client_fd, F_SETFL, flags | O_NONBLOCK);

                // 3. 레이더망(epoll)에 등록 (정답: EPOLLIN!)
                struct epoll_event event;
                event.events = EPOLLIN; // "이 손님이 메시지(읽기)를 보내는지 감시해!"
                event.data.fd = new_client_fd;
                epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, new_client_fd, &event);

                // 4. ★ Map에 저장 ★ (ClientSocket 뼈대 객체 생성)
                _clients[new_client_fd] = new ClientSocket(new_client_fd, client_addr);

                std::cout << "New client connected! FD: " << new_client_fd << std::endl;
            }

            // ========================================================
            // 분기 B: active_fd가 _clients 맵에 존재한다면? (기존 손님의 메시지!)
            // ========================================================
            else if (_clients.find(active_fd) != _clients.end()) {
                char buffer[1024];
                std::memset(buffer, 0, sizeof(buffer));

                // 1. 데이터 읽기
                int bytes_read = recv(active_fd, buffer, sizeof(buffer), 0);

                if (bytes_read > 0) {
                    // 정상적으로 메시지를 받음 -> 에코(그대로 돌려주기)
                    send(active_fd, buffer, bytes_read, 0);
                    std::cout << "Echoed to FD " << active_fd << ": " << buffer;
                }
                else if (bytes_read == 0) {
                    // ★ 클라이언트가 연결을 끊고 나감! (TCP FIN 패킷 수신) ★
                    std::cout << "Client disconnected. FD: " << active_fd << std::endl;
                    epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, active_fd, 0); // 레이더망에서 제거
					close(active_fd); // 소켓 닫기
					delete _clients[active_fd]; // ClientSocket 객체 삭제
					_clients.erase(active_fd); // 맵에서 제거
                }
                else {
                    // 에러 발생 (EAGAIN 등 처리 로직)
                }
            }
        }
    }
}
