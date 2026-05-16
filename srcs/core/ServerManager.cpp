#include "ServerManager.hpp"

void ServerManager::dispatchEvents(int fd, uint32_t evs)
{
    if (_servers.find(fd) != _servers.end())
    {
        if (evs & EPOLLIN){
            handleAccept(fd);
            return ;
        }
    }
}

void ServerManager::handleAccept(int fd)
{
}

ServerManager::ServerManager() : _epoll_fd(-1)
{
	std::cout << "ServerManager : constructor called\n";
}

ServerManager::~ServerManager()
{
    if (_epoll_fd >= 0) {
        close(_epoll_fd);
    }
	for (std::map<int, ServerSocket*>::iterator it = _servers.begin(); it != _servers.end(); ++it) {
		delete it->second;
	}
	for (std::map<int, ClientSocket*>::iterator it = _clients.begin(); it != _clients.end(); ++it) {
		delete it->second;
	}
	std::cout << "ServerManager : destructor called\n";
}

void ServerManager::init(const Config& config) {
    //epoll 생성
    _epoll_fd = epoll_create(1);
	if (_epoll_fd < 0){
		throw std::runtime_error("epoll_create() failed\n");
	}

    // 서버블록을 포트+ip로 그룹화
    const std::vector<ServerBlock>& serverBlocks = config.getServerBlocks();
    std::map<std::pair<std::string, int>, std::vector<const ServerBlock*> > grouping;
    for (size_t i = 0; i < serverBlocks.size(); ++i) {
        std::pair<std::string, int> key(serverBlocks[i].getHost(), serverBlocks[i].getPort());
        grouping[key].push_back(&serverBlocks[i]);
    }

    // 서버 소켓 할당 및 초기화
    std::map<std::pair<std::string, int>, std::vector<const ServerBlock*> >::iterator it;
    for (it = grouping.begin(); it != grouping.end(); ++it) {
        ServerSocket* server = new ServerSocket();
        try {
            server->init(it->first.first, it->first.second, it->second);
            _servers[server->getFd()] = server;
        } catch (const std::exception& e) {
            std::cerr << "ServerManager: Error initializing ServerManager: " << e.what() << std::endl;
            throw;
        }
    }

    // 서버 소켓을 epoll에 등록
    for (std::map<int, ServerSocket*>::iterator it = _servers.begin(); it != _servers.end(); ++it) {
        struct epoll_event event;
        event.events = EPOLLIN;
        event.data.fd = it->first;
        if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, event.data.fd, &event) < 0){
            throw std::runtime_error("epoll_ctl() failed\n");
        }
    }

}

void ServerManager::run() {
    struct epoll_event events[MAX_EVENTS];

    while (true) {
        int event_count = epoll_wait(_epoll_fd, events, MAX_EVENTS, -1);
        if (event_count < 0) {continue;}

        for (int i = 0; i < event_count; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;
            dispatchEvents(fd, ev);
        }
    }
}
