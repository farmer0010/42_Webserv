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

	// 서버 소켓을 epoll에 등록 (읽기 이벤트)
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
	event.events = EPOLLIN | EPOLLET;
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

	// 클라이언트 소켓: 읽기/쓰기 처리
	if (_clients.find(fd) != _clients.end()) {
		ClientSocket* client = _clients[fd];

		if (evs & (EPOLLERR | EPOLLHUP)) {
			removeClient(fd);
			return;
		}
		if (evs & EPOLLIN)
			client->handleRead();
		if (evs & EPOLLOUT)
			client->handleWrite();

		ClientState state = client->getState();
		if (state == WRITING)
			setEpollEvents(fd, EPOLLOUT | EPOLLET);
		else if (state == DONE)
			removeClient(fd);
	}
}
