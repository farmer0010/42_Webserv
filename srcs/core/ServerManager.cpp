#include "ServerManager.hpp"

#include <sys/wait.h>
#include <csignal>
#include <ctime>
#include <vector>
#include <iostream>
#include <arpa/inet.h>

// 역할: epoll fd 를 무효값(-1)으로 두고 컨테이너만 비워둔 상태로 만든다.
// 책임: 실제 리소스 획득은 init() 으로 위임. 소멸자에서 안전한 close 분기가 가능하도록
//       "아직 아무것도 보유하지 않음" 을 보장.
ServerManager::ServerManager() : _epoll_fd(-1)
{
}

// 역할: 프로세스 종료 직전 보유 중인 epoll/서버/클라이언트 핸들을 한 번에 정리.
// 책임: ServerManager 가 소유한 동적 객체(ServerSocket*, ClientSocket*) 의 수명 종결자.
//       CGI 매핑은 ClientSocket 정리 과정에서 removeClient → removeCgi 경로로 정리되거나,
//       프로세스 종료와 함께 소멸하므로 여기서 별도 sweep 하지 않는다.
ServerManager::~ServerManager()
{
	if (_epoll_fd >= 0)
		close(_epoll_fd);
	for (std::map<int, ServerSocket*>::iterator it = _servers.begin(); it != _servers.end(); ++it)
		delete it->second;
	for (std::map<int, ClientSocket*>::iterator it = _clients.begin(); it != _clients.end(); ++it)
		delete it->second;
}

// 역할: Config 의 서버블록을 (host, port) 키로 그룹화해 ServerSocket 인스턴스를 만들고,
//       각 listen fd 를 epoll 에 등록한다.
// 책임: 네트워크 레이어의 "기동" 단계. 동일 host:port 에 묶인 서버블록 리스트를
//       ServerSocket 에 넘겨 보관하게 하여, 이후 Host 헤더 매칭은 ClientSocket 이
//       해당 리스트에서 선택하도록 한다. 부분 실패 시 이미 생성된 ServerSocket 은
//       소멸자에서 자동 회수되도록 _servers 에 등록 후 throw.
void ServerManager::init(const Config& config)
{
	_epoll_fd = epoll_create(1);
	if (_epoll_fd < 0)
		throw std::runtime_error("epoll_create() failed");

	const std::vector<ServerBlock>& serverBlocks = config.getServerBlocks();
	std::map<std::pair<std::string, int>, std::vector<const ServerBlock*> > grouping;
	for (size_t i = 0; i < serverBlocks.size(); ++i) {
		std::pair<std::string, int> key(serverBlocks[i].getHost(), serverBlocks[i].getPort());
		grouping[key].push_back(&serverBlocks[i]);
	}

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

	for (std::map<int, ServerSocket*>::iterator sit = _servers.begin(); sit != _servers.end(); ++sit) {
		struct epoll_event event;
		event.events = EPOLLIN;
		event.data.fd = sit->first;
		if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, sit->first, &event) < 0)
			throw std::runtime_error("epoll_ctl() failed");
	}
}

// 역할: 무한 이벤트 루프. epoll_wait 로 준비된 fd 를 모아 dispatchEvents 에 흘리고,
//       매 사이클 sweepTimeouts 로 만료된 연결을 회수.
// 책임: 단일 epoll 인스턴스에 모든 I/O 를 모으는 reactor 의 진입점. 실제 I/O 처리는
//       ClientSocket/ServerSocket 으로 위임하고, 여기서는 분배와 주기 작업만 담당한다.
void ServerManager::run()
{
	struct epoll_event events[MAX_EVENTS];

	while (true) {
		int event_count = epoll_wait(_epoll_fd, events, MAX_EVENTS, EPOLL_WAIT_INTERVAL_MS);
		if (event_count < 0)
			continue;
		for (int i = 0; i < event_count; ++i)
			dispatchEvents(events[i].data.fd, events[i].events);
		sweepTimeouts();
	}
}

// 역할: 리스닝 fd 에서 한 건 accept, 논블로킹 전환 후 ClientSocket 을 만들고 epoll 에 등록.
// 책임: "새 클라이언트" 가 시스템에 진입하는 유일 경로. 등록 실패 시 즉시 fd/객체를
//       회수해 매핑 누수와 stale fd 가 epoll 에 남지 않도록 한다.
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
		std::cerr << "[epoll_ctl] ADD failed fd=" << client_fd << std::endl;
		delete client;
		_clients.erase(client_fd);
		return;
	}

	std::cout << "[accept] fd=" << client_fd
			  << " from " << inet_ntoa(client_addr.sin_addr)
			  << ":" << ntohs(client_addr.sin_port) << std::endl;
}

// 역할: 클라이언트 fd 한 건과 이에 묶인 CGI 자식/파이프/매핑까지 일괄 정리.
// 책임: ClientSocket 의 단일 종결 경로. _cgi_to_client 의 stale 엔트리 sweep,
//       살아있는 CGI 자식의 SIGKILL+reap, epoll 제거, 객체 해제 순으로 진행하여
//       이후 사이클에서 NULL 역참조나 좀비/orphan 이 생기지 않도록 보장.
void ServerManager::removeClient(int client_fd)
{
	std::map<int, ClientSocket*>::iterator it = _clients.find(client_fd);
	if (it == _clients.end())
		return;

	std::vector<int> orphan_cgi_fds;
	for (std::map<int, int>::iterator cit = _cgi_to_client.begin();
		 cit != _cgi_to_client.end(); ++cit) {
		if (cit->second == client_fd)
			orphan_cgi_fds.push_back(cit->first);
	}
	for (size_t i = 0; i < orphan_cgi_fds.size(); ++i)
		removeCgi(orphan_cgi_fds[i]);

	pid_t pid = it->second->getCgiPid();
	if (pid > 0) {
		kill(pid, SIGKILL);
		waitpid(pid, NULL, 0);
	}

	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
	delete it->second;
	_clients.erase(it);
	std::cout << "[close] client fd=" << client_fd << std::endl;
}

// 역할: CGI 파이프 fd 를 논블로킹으로 만들고 epoll 에 등록 + cgi→client 매핑 기록.
// 책임: CGI 가 단일 epoll 루프에 통합되는 진입점. 실패 시 false 만 돌려주고 정리
//       책임은 호출자(dispatchEvents) 에 위임 — 부분 등록 상태가 남지 않도록 한다.
bool ServerManager::addCgiFd(int cgi_fd, int client_fd, uint32_t events)
{
	int flags = fcntl(cgi_fd, F_GETFL, 0);
	if (flags < 0 || fcntl(cgi_fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return false;

	struct epoll_event event;
	event.events = events;
	event.data.fd = cgi_fd;
	if (epoll_ctl(_epoll_fd, EPOLL_CTL_ADD, cgi_fd, &event) < 0)
		return false;
	_cgi_to_client[cgi_fd] = client_fd;
	return true;
}

// 역할: CGI 파이프 fd 를 epoll 에서 떼고 cgi→client 매핑을 지운다.
// 책임: 파이프 fd 자체의 close 는 ClientSocket(또는 Cgi) 측 소유이므로 여기서는 닫지 않는다.
//       매핑/이벤트 등록만 ServerManager 의 책임.
void ServerManager::removeCgi(int cgi_fd)
{
	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, cgi_fd, NULL);
	_cgi_to_client.erase(cgi_fd);
}

// 역할: 이미 등록된 fd 의 관심 이벤트 마스크를 교체(EPOLL_CTL_MOD).
// 책임: READING↔WRITING 전환 같은 상태 변화에 따른 epoll 모드 갱신만 담당.
void ServerManager::setEpollEvents(int fd, uint32_t events)
{
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;
	epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

// 역할: 매 이벤트 루프 사이클에서 만료된 클라이언트(idle 한도 초과 또는 CGI 실행시간 초과)를 정리.
// 책임: 정상 응답 흐름에 끼어들지 않고 백그라운드 sweep 만 담당. 만료 사유와 무관하게
//       클라이언트는 응답 없이 종료(추후 504 등 응답 합류 가능). CGI 자식은 SIGKILL + reap 으로
//       좀비/매달림 방지.
void ServerManager::sweepTimeouts()
{
	if (_clients.empty())
		return;

	time_t now = time(NULL);
	std::vector<int> expired;

	for (std::map<int, ClientSocket*>::iterator it = _clients.begin();
		 it != _clients.end(); ++it) {
		ClientSocket* client = it->second;
		bool idle_expired = (now - client->getLastActiveTime()) > CLIENT_IDLE_TIMEOUT;
		bool cgi_expired  = client->getCgiStartTime() > 0 &&
							(now - client->getCgiStartTime()) > CGI_TIMEOUT;
		if (idle_expired || cgi_expired)
			expired.push_back(it->first);
	}

	for (size_t i = 0; i < expired.size(); ++i) {
		int fd = expired[i];
		std::map<int, ClientSocket*>::iterator it = _clients.find(fd);
		if (it == _clients.end())
			continue;
		ClientSocket* client = it->second;

		bool cgi_expired = client->getCgiStartTime() > 0 &&
						   (now - client->getCgiStartTime()) > CGI_TIMEOUT;
		std::cerr << "[timeout] fd=" << fd
				  << (cgi_expired ? " (cgi)" : " (idle)") << std::endl;

		pid_t pid = client->getCgiPid();
		if (pid > 0) {
			kill(pid, SIGKILL);
			waitpid(pid, NULL, 0);
		}
		int write_fd = client->getCgiWriteFd();
		int read_fd  = client->getCgiReadFd();
		if (write_fd >= 0 && _cgi_to_client.count(write_fd)) removeCgi(write_fd);
		if (read_fd  >= 0 && _cgi_to_client.count(read_fd))  removeCgi(read_fd);
		removeClient(fd);
	}
}

// 역할: epoll_wait 에서 빠져나온 단일 이벤트를 fd 종류(서버/CGI/클라이언트)별로 분기 처리.
// 책임: I/O 자체는 ClientSocket 으로 위임하고, ServerManager 는 상태 전이(ClientState)에 따른
//       epoll 등록/해제와 CGI 파이프 라이프사이클(자식 reap 포함)만 조율한다.
//       peer 종료(EPOLLERR/EPOLLHUP) 동반 시에도 같은 사이클의 EPOLLIN/OUT 을 먼저 처리해
//       마지막 응답 드레인 기회를 보장한다.
void ServerManager::dispatchEvents(int fd, uint32_t evs)
{
	if (_servers.find(fd) != _servers.end()) {
		if (evs & EPOLLIN)
			handleAccept(fd);
		return;
	}

	if (_cgi_to_client.find(fd) != _cgi_to_client.end()) {
		int client_fd = _cgi_to_client[fd];
		std::map<int, ClientSocket*>::iterator cit = _clients.find(client_fd);
		if (cit == _clients.end() || cit->second == NULL) {
			std::cerr << "[cgi] stale mapping cgi_fd=" << fd
					  << " client_fd=" << client_fd << " (cleanup)" << std::endl;
			removeCgi(fd);
			return;
		}
		ClientSocket* client = cit->second;

		if (evs & EPOLLOUT)
			client->handleCgiWrite();
		if (evs & (EPOLLIN | EPOLLHUP))
			client->handleCgiRead();
		if ((evs & EPOLLERR) && !(evs & EPOLLIN)) {
			removeCgi(fd);
			removeClient(client_fd);
			return;
		}

		ClientState state = client->getState();
		if (state == CGI_READING_OUTPUT) {
			removeCgi(fd);
			int read_fd = client->getCgiReadFd();
			if (read_fd < 0 || !addCgiFd(read_fd, client_fd, EPOLLIN)) {
				std::cerr << "[cgi] addCgiFd(read) failed client_fd=" << client_fd << std::endl;
				pid_t pid = client->getCgiPid();
				if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
				removeClient(client_fd);
			}
		} else if (state == WRITING) {
			removeCgi(fd);
			pid_t pid = client->getCgiPid();
			if (pid > 0)
				waitpid(pid, NULL, WNOHANG);
			setEpollEvents(client_fd, EPOLLOUT);
		} else if (state == DONE) {
			removeCgi(fd);
			pid_t pid = client->getCgiPid();
			if (pid > 0)
				waitpid(pid, NULL, WNOHANG);
			removeClient(client_fd);
		}
		return;
	}

	if (_clients.find(fd) != _clients.end()) {
		ClientSocket* client = _clients[fd];

		bool peer_gone = (evs & (EPOLLERR | EPOLLHUP)) != 0;

		if (evs & EPOLLIN)
			client->handleRead();
		if (evs & EPOLLOUT)
			client->handleWrite();

		ClientState state = client->getState();

		if (peer_gone && state != WRITING) {
			int write_fd = client->getCgiWriteFd();
			int read_fd  = client->getCgiReadFd();
			if (write_fd >= 0 && _cgi_to_client.count(write_fd)) removeCgi(write_fd);
			if (read_fd  >= 0 && _cgi_to_client.count(read_fd))  removeCgi(read_fd);
			pid_t pid = client->getCgiPid();
			if (pid > 0)
				waitpid(pid, NULL, WNOHANG);
			removeClient(fd);
			return;
		}

		if (state == WRITING)
			setEpollEvents(fd, EPOLLOUT);
		else if (state == CGI_WRITING_BODY) {
			int write_fd = client->getCgiWriteFd();
			if (write_fd < 0 || !addCgiFd(write_fd, fd, EPOLLOUT)) {
				std::cerr << "[cgi] addCgiFd(write) failed client_fd=" << fd << std::endl;
				pid_t pid = client->getCgiPid();
				if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
				int read_fd = client->getCgiReadFd();
				if (read_fd >= 0 && _cgi_to_client.count(read_fd)) removeCgi(read_fd);
				removeClient(fd);
			}
		}
		else if (state == READING)
			setEpollEvents(fd, EPOLLIN);
		else if (state == DONE)
			removeClient(fd);
	}
}
