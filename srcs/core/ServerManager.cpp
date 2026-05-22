#include "ServerManager.hpp"

ServerManager::ServerManager() : _epoll_fd(-1)
{
}

ServerManager::~ServerManager()
{
	if (_epoll_fd >= 0)
		close(_epoll_fd);
	for (std::map<int, ServerSocket*>::iterator it = _servers.begin(); it != _servers.end(); ++it)
		delete it->second;
	for (std::map<int, ClientSocket*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		delete it->second;
}

void ServerManager::init(const Config& config)
{
	_epoll_fd = epoll_create(1);
	if (_epoll_fd < 0)
		throw std::runtime_error("epoll_create() failed");
	// 서버 블록을 ip+port로 그룹화
	const std::vector<ServerBlock>& serverBlocks = config.getServerBlocks();
	std::map<std::pair<std::string, int>, std::vector<const ServerBlock*> > grouping;
	for (size_t i = 0; i < serverBlocks.size(); ++i) {
		std::pair<std::string, int> key(serverBlocks[i].getHost(), serverBlocks[i].getPort());
		grouping[key].push_back(&serverBlocks[i]);
	}

	// 서버 소켓 생성 및 초기화
	std::map<std::pair<std::string, int>, std::vector<const ServerBlock*> >::iterator it;
	for (it = grouping.begin(); it != grouping.end(); ++it) {
		ServerSocket* server = new ServerSocket();
		try {
			server->init(it->first.first, it->first.second, it->second);
			_servers[server->getFd()] = server;
		} catch (const std::exception& e) {
			delete server;
			std::cerr << "ServerManager: " << e.what() << std::endl;
			throw;
		}
	}

	// 서버 소켓을 epoll에 등록
	for (std::map<int, ServerSocket*>::iterator sit = _servers.begin(); sit != _servers.end(); ++sit) {
		struct epoll_event event;
		event.events = EPOLLIN;
		event.data.fd = sit->first;
		if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, sit->first, &event) < 0)
			throw std::runtime_error("epoll_ctl() failed");
	}
}

void ServerManager::run()
{
	struct epoll_event events[MAX_EVENTS];

	while (true) {
		int event_count = epoll_wait(_epoll_fd, events, MAX_EVENTS, -1);
		if (event_count < 0)
			continue;
		for (int i = 0; i < event_count; ++i)
			dispatchEvents(events[i].data.fd, events[i].events);
	}
}

void ServerManager::handleAccept(int server_fd)
{
	struct sockaddr_in client_addr;
	socklen_t addr_len = sizeof(client_addr);

	int client_fd = accept(server_fd, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);
	if (client_fd < 0)
		return;

	int flags = fcntl(client_fd, F_GETFL, 0);
	if (flags < 0 || fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
		close(client_fd);
		return;
	}

	ClientSocket* client = new ClientSocket();
	client->init(client_fd, client_addr, _servers[server_fd]);
	_clients[client_fd] = client;

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.fd = client_fd;
	if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
		delete client;
		_clients.erase(client_fd);
	}
}

void ServerManager::removeClient(int client_fd)
{
	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
	delete _clients[client_fd];
	_clients.erase(client_fd);
}

// CGI 파이프 fd를 epoll에 등록하고 _cgi_to_client 맵에 기록
void ServerManager::addCgiFd(int cgi_fd, int client_fd, uint32_t events)
{
	struct epoll_event event;
	event.events = events;
	event.data.fd = cgi_fd;
	if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, cgi_fd, &event) < 0)
		return;
	_cgi_to_client[cgi_fd] = client_fd;
}

// CGI 파이프 fd를 epoll에서 제거하고 닫기
void ServerManager::removeCgi(int cgi_fd)
{
	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, cgi_fd, NULL);
	close(cgi_fd);
	_cgi_to_client.erase(cgi_fd);
}

void ServerManager::setEpollEvents(int fd, uint32_t events)
{
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;
	epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

void ServerManager::dispatchEvents(int fd, uint32_t evs)
{
	// 서버 소켓: 새 연결 수락
	if (_servers.find(fd) != _servers.end()) {
		if (evs & EPOLLIN)
			handleAccept(fd);
		return;
	}

	// CGI 파이프 fd: CGI 프로세스와의 I/O 처리
	if (_cgi_to_client.find(fd) != _cgi_to_client.end()) {
		int client_fd = _cgi_to_client[fd];
		ClientSocket* client = _clients[client_fd];

		// EPOLLOUT: write pipe → CGI stdin에 body 전송
		if (evs & EPOLLOUT)
			client->handleCgiWrite();
		// EPOLLIN | EPOLLHUP: read pipe → CGI stdout 읽기
		// EPOLLHUP는 CGI 프로세스 종료 신호, 남은 데이터를 다 읽을 때까지 처리
		if (evs & (EPOLLIN | EPOLLHUP))
			client->handleCgiRead();
		// EPOLLERR: 파이프 에러 → 연결 정리
		if ((evs & EPOLLERR) && !(evs & EPOLLIN)) {
			removeCgi(fd);
			removeClient(client_fd);
			return;
		}

		ClientState state = client->getState();
		if (state == CGI_READING_OUTPUT) {
			// write 완료: write pipe fd 제거, read pipe fd 등록
			removeCgi(fd);
			int read_fd = client->getCgiReadFd();
			if (read_fd >= 0)
				addCgiFd(read_fd, client_fd, EPOLLIN);
		} else if (state == WRITING) {
			// CGI 출력 수신 완료: read pipe fd 제거, 클라이언트 fd를 EPOLLOUT으로 전환
			removeCgi(fd);
			setEpollEvents(client_fd, EPOLLOUT);
		} else if (state == DONE) {
			removeCgi(fd);
			removeClient(client_fd);
		}
		return;
	}

	// 클라이언트 소켓: 읽기/쓰기 처리
	if (_clients.find(fd) != _clients.end()) {
		ClientSocket* client = _clients[fd];

		if (evs & (EPOLLERR | EPOLLHUP)) {
			// CGI 실행 중 클라이언트가 끊긴 경우 열린 CGI fd도 함께 정리
			int write_fd = client->getCgiWriteFd();
			int read_fd  = client->getCgiReadFd();
			if (write_fd >= 0 && _cgi_to_client.count(write_fd)) removeCgi(write_fd);
			if (read_fd  >= 0 && _cgi_to_client.count(read_fd))  removeCgi(read_fd);
			removeClient(fd);
			return;
		}
		if (evs & EPOLLIN)
			client->handleRead();
		if (evs & EPOLLOUT)
			client->handleWrite();

		ClientState state = client->getState();
		if (state == WRITING)
			setEpollEvents(fd, EPOLLOUT);
		else if (state == CGI_WRITING_BODY) {
			// processRequest가 CGI 진입: write pipe fd를 epoll에 등록
			int write_fd = client->getCgiWriteFd();
			if (write_fd >= 0)
				addCgiFd(write_fd, fd, EPOLLOUT);
		}
		else if (state == READING)   // keep-alive
			setEpollEvents(fd, EPOLLIN);
		else if (state == DONE)
			removeClient(fd);
	}
}
