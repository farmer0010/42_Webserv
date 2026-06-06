#include "ServerManager.hpp"

#include <sys/wait.h>
#include <csignal>
#include <ctime>
#include <vector>
#include <iostream>
#include <arpa/inet.h>

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
		// 인터벌을 두어 idle/CGI 만료 sweep 이 주기적으로 돌 수 있게 함
		int event_count = epoll_wait(_epoll_fd, events, MAX_EVENTS, EPOLL_WAIT_INTERVAL_MS);
		if (event_count < 0)
			continue;
		for (int i = 0; i < event_count; ++i)
			dispatchEvents(events[i].data.fd, events[i].events);
		sweepTimeouts();
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
		std::cerr << "[epoll_ctl] ADD failed fd=" << client_fd << std::endl;
		delete client;
		_clients.erase(client_fd);
		return;
	}

	std::cout << "[accept] fd=" << client_fd
			  << " from " << inet_ntoa(client_addr.sin_addr)
			  << ":" << ntohs(client_addr.sin_port) << std::endl;
}

void ServerManager::removeClient(int client_fd)
{
	std::map<int, ClientSocket*>::iterator it = _clients.find(client_fd);
	if (it == _clients.end())
		return;

	// 이 client 를 참조하는 _cgi_to_client 잔존 엔트리 일괄 정리.
	// 정상 흐름에선 호출자가 미리 정리하지만, 누락 경로(예: client 분기 state==DONE)에서
	// stale 엔트리가 남으면 다음 epoll 사이클에서 _clients[client_fd]==NULL 역참조로
	// SIGSEGV. 여기서 마지막 가드.
	std::vector<int> orphan_cgi_fds;
	for (std::map<int, int>::iterator cit = _cgi_to_client.begin();
		 cit != _cgi_to_client.end(); ++cit) {
		if (cit->second == client_fd)
			orphan_cgi_fds.push_back(cit->first);
	}
	for (size_t i = 0; i < orphan_cgi_fds.size(); ++i)
		removeCgi(orphan_cgi_fds[i]);

	// 살아있는 CGI 자식이 있으면 SIGKILL + 블로킹 waitpid 로 좀비/orphan 방지.
	// kill 직후라 블로킹해도 즉시 reap.
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

// CGI 파이프 fd를 non-blocking으로 설정 후 epoll에 등록.
// 실패 시 false 반환 — 호출자가 클라이언트를 정리해야 함.
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

// CGI 파이프 fd를 epoll에서 제거하고 닫기
void ServerManager::removeCgi(int cgi_fd)
{
	epoll_ctl(_epoll_fd, EPOLL_CTL_DEL, cgi_fd, NULL);
	_cgi_to_client.erase(cgi_fd);
}

void ServerManager::setEpollEvents(int fd, uint32_t events)
{
	struct epoll_event event;
	event.events = events;
	event.data.fd = fd;
	epoll_ctl(_epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

// run 루프가 매 cycle 호출. 만료된 클라이언트(idle 또는 CGI 실행시간 초과)를 정리.
// - idle: 마지막 read/write 로부터 CLIENT_IDLE_TIMEOUT 초 경과
// - CGI:  cgi 시작 시점으로부터 CGI_TIMEOUT 초 경과 → 자식 SIGKILL 후 회수
// 응답은 보내지 않고 즉시 종료 (504 등 응답은 후속 작업)
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

		// CGI 가 살아있으면 SIGKILL 로 강제 종료 후 회수
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
		// 정합성 가드: client 가 이미 사라진 stale 매핑이면 NULL 역참조 회피.
		// 이 상태에 도달하면 어딘가에 cleanup 누락이 있다는 신호 — 로그로 표식.
		std::map<int, ClientSocket*>::iterator cit = _clients.find(client_fd);
		if (cit == _clients.end() || cit->second == NULL) {
			std::cerr << "[cgi] stale mapping cgi_fd=" << fd
					  << " client_fd=" << client_fd << " (cleanup)" << std::endl;
			removeCgi(fd);
			return;
		}
		ClientSocket* client = cit->second;

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
			if (read_fd < 0 || !addCgiFd(read_fd, client_fd, EPOLLIN)) {
				// read pipe 등록 실패 → 무한 매달림 방지를 위해 즉시 정리
				std::cerr << "[cgi] addCgiFd(read) failed client_fd=" << client_fd << std::endl;
				pid_t pid = client->getCgiPid();
				if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
				removeClient(client_fd);
			}
		} else if (state == WRITING) {
			// CGI 출력 수신 완료: read pipe fd 제거, 클라이언트 fd를 EPOLLOUT으로 전환
			removeCgi(fd);
			// CGI 정상 종료(EOF) 시점: 자식 회수
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

	// 클라이언트 소켓: 읽기/쓰기 처리
	if (_clients.find(fd) != _clients.end()) {
		ClientSocket* client = _clients[fd];

		// EPOLLERR / EPOLLHUP 는 peer 종료 신호. 다만 같은 dispatch 에 EPOLLIN 이
		// 동반된 경우(마지막 요청 직후 FIN) 즉시 종료하면 마지막 요청 데이터를
		// 드레인하기 전에 닫혀 응답이 못 나간다. 먼저 read/write 를 처리한 뒤,
		// 더 송신할 게 없을 때만 정리한다. recv == 0 이면 handleRead 가 _state 를
		// DONE 으로 전이시키므로 정상 흐름에 맡길 수 있음.
		bool peer_gone = (evs & (EPOLLERR | EPOLLHUP)) != 0;

		if (evs & EPOLLIN)
			client->handleRead();
		if (evs & EPOLLOUT)
			client->handleWrite();

		ClientState state = client->getState();

		// peer 가 닫혔고 송신 잔여가 없으면(또는 에러) 즉시 정리
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
			// processRequest가 CGI 진입: write pipe fd를 epoll에 등록
			int write_fd = client->getCgiWriteFd();
			if (write_fd < 0 || !addCgiFd(write_fd, fd, EPOLLOUT)) {
				// write pipe 등록 실패 → CGI 고아 방지를 위해 즉시 정리
				std::cerr << "[cgi] addCgiFd(write) failed client_fd=" << fd << std::endl;
				pid_t pid = client->getCgiPid();
				if (pid > 0) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }
				int read_fd = client->getCgiReadFd();
				if (read_fd >= 0 && _cgi_to_client.count(read_fd)) removeCgi(read_fd);
				removeClient(fd);
			}
		}
		else if (state == READING)   // keep-alive
			setEpollEvents(fd, EPOLLIN);
		else if (state == DONE)
			removeClient(fd);
	}
}
