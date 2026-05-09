#ifndef SERVERMANAGER_HPP
# define SERVERMANAGER_HPP

# include "ServerSocket.hpp"
# include "ClientSocket.hpp"

# include <sys/epoll.h>
# include <map>

# define MAX_EVENTS 1024

class ServerManager
{
	private:
		int	_epoll_fd;
		std::map<int, ServerSocket*> _servers;
		std::map<int, ClientSocket*> _clients;
	public:
		ServerManager(int port);
		~ServerManager();

		void run();
};

#endif
