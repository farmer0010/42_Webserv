#ifndef SERVERMANAGER_HPP
# define SERVERMANAGER_HPP

# include "Config.hpp"
# include "ServerSocket.hpp"
# include "ClientSocket.hpp"

# include <sys/epoll.h>
# include <cerrno>
# include <map>

# define MAX_EVENTS 1024

class ServerManager
{
	private:
		int	_epoll_fd;
		std::map<int, ServerSocket*> _servers;
		std::map<int, ClientSocket*> _clients;

		void dispatchEvents(int fd, uint32_t evs);
		void handleAccept(int server_fd);
		void removeClient(int client_fd);
		void setEpollEvents(int fd, uint32_t events);

	public:
		ServerManager();
		~ServerManager();

		void init(const Config& config);
		void run();
};

#endif
