#ifndef SERVERMANAGER_HPP
# define SERVERMANAGER_HPP

# include "Config.hpp"
# include "ServerSocket.hpp"
# include "ClientSocket.hpp"

# include <sys/epoll.h>
# include <map>

# define MAX_EVENTS 1024
# define EPOLL_WAIT_INTERVAL_MS 1000
# define CLIENT_IDLE_TIMEOUT 60
# define CGI_TIMEOUT 30

class ServerManager
{
	private:
		int	_epoll_fd;
		std::map<int, ServerSocket*> _servers;
		std::map<int, ClientSocket*> _clients;
		std::map<int, int>           _cgi_to_client;

		void dispatchEvents(int fd, uint32_t evs);                          // epoll 이벤트 1건을 fd 종류별로 분기.
		void handleAccept(int server_fd);                                   // 새 연결 accept + 논블로킹 + epoll 등록.
		void removeClient(int client_fd);                                   // 클라이언트 + 잔존 CGI 리소스 일괄 정리.
		void setEpollEvents(int fd, uint32_t events);                       // EPOLLIN/OUT 모드 전환.
		bool addCgiFd(int cgi_fd, int client_fd, uint32_t events);          // CGI 파이프 fd 를 epoll 에 등록 + 매핑 기록.
		void removeCgi(int cgi_fd);                                         // CGI 파이프 fd 를 epoll 에서 제거 + 매핑 삭제.
		void sweepTimeouts();                                               // idle/CGI 만료 클라이언트 회수.

	public:
		ServerManager();                                                    // _epoll_fd 만 -1 로 두는 초기 상태.
		~ServerManager();                                                   // epoll/server/client 핸들 일괄 해제.

		void init(const Config& config);                                    // 설정 기반 리스닝 소켓 구성 + epoll 등록.
		void run();                                                         // 메인 이벤트 루프: epoll_wait → dispatch → sweep.
};

#endif
