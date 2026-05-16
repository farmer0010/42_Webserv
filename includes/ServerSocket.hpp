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
# include <netdb.h>
# include <arpa/inet.h>

class ServerSocket
{
	private:
		int	_server_fd;
		struct sockaddr_in _address;
		std::vector<const ServerBlock*> _server_blocks;
	public:
		ServerSocket();
		void init(std::string host, int port, const std::vector<const ServerBlock*>& serverBlocks);
		~ServerSocket();

		int	getFd(){ return this->_server_fd; };
		const std::vector<const ServerBlock*>& getServerBlocks() const { return _server_blocks; }
};

#endif
