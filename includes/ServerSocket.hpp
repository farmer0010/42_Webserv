#ifndef SERVERSOCKET_HPP
# define SERVERSOCKET_HPP

# include <stdexcept>
# include <iostream>
# include <string>
# include <cstring>      // memset
# include <sys/socket.h> // socket, bind, listen, accept, recv, send
# include <netinet/in.h> // sockaddr_in, htons
# include <unistd.h>
# include <fcntl.h>

# define PORT 8080

class ServerSocket
{
	private:
		int	_server_fd;
		struct sockaddr_in _address;
	public:
		ServerSocket();
		~ServerSocket();

		int	getServerfd();
};

#endif
