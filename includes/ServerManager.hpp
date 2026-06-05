#ifndef SERVERMANAGER_HPP
# define SERVERMANAGER_HPP

# include "Config.hpp"
# include "ServerSocket.hpp"
# include "ClientSocket.hpp"

# include <sys/epoll.h>
# include <map>

# define MAX_EVENTS 1024
# define EPOLL_WAIT_INTERVAL_MS 1000
# define CLIENT_IDLE_TIMEOUT 60   // 초: 마지막 read/write 이후 idle 한도
# define CGI_TIMEOUT 30           // 초: CGI 시작 이후 최대 실행 시간

class ServerManager
{
	private:
		int	_epoll_fd;
		std::map<int, ServerSocket*> _servers;
		std::map<int, ClientSocket*> _clients;
		std::map<int, int>           _cgi_to_client; // cgi pipe fd → client fd

		void dispatchEvents(int fd, uint32_t evs);
		void handleAccept(int server_fd);
		void removeClient(int client_fd);
		void setEpollEvents(int fd, uint32_t events);
		bool addCgiFd(int cgi_fd, int client_fd, uint32_t events);
		void removeCgi(int cgi_fd);
		void sweepTimeouts();

	public:
		ServerManager();
		~ServerManager();

		void init(const Config& config);
		void run();
};

#endif
