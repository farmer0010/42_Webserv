#ifndef SERVERSOCKET_HPP
# define SERVERSOCKET_HPP

# include "Config.hpp"

# include <stdexcept>
# include <iostream>
# include <string>
# include <cstring>
# include <sys/socket.h> // socket, bind, listen, accept, recv, send
# include <netinet/in.h> // sockaddr_in, htons
# include <unistd.h>
# include <fcntl.h>

class ServerSocket
{
	private:
		int	_server_fd;
		struct sockaddr_in _address;
		std::vector<ServerBlock> _server_blocks;
	public:
		ServerSocket();
		void init(int port,const std::vector<ServerBlock>& serverBlocks);
		~ServerSocket();

		int	getFd(){ return this->_server_fd; };
};

#endif
