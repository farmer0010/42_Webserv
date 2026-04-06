#include "ServerManager.hpp"

ServerManager::ServerManager()
{
	epfd = epoll_create(1);
	if (epfd < 0){
		throw std::runtime_error("epoll_create() failed\n");
	}


}
