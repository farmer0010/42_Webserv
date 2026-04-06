#ifndef SERVERMANAGER_HPP
# define SERVERMANAGER_HPP

# include "ServerSocket.hpp"
# include "ClientSocket.hpp"

# include <sys/epoll.h>
# include <map>

class ServerManager
{
	private:
		int	epfd;
		std::map<int, ServerSocket*> _server;
		std::map<int, ClientSocket*> _client;
	public:
		ServerManager();
		~ServerManager();
};

#endif
