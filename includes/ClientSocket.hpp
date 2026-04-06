#ifndef CLIENTSOCKET_HPP
# define CLIENTSOCKET_HPP

# include <stdexcept>
# include <iostream>
# include <string>
# include <cstring>      // memset
# include <sys/socket.h> // socket, bind, listen, accept, recv, send
# include <netinet/in.h> // sockaddr_in, htons
# include <unistd.h>
# include <fcntl.h>
# include <vector>

enum ClientState{
	READING,
	WRITING
};

struct StateMachine{
	ClientState		state;
	ClientSocket*	fd;
};

class ClientSocket
{
	private:
		int	_client_fd;
		struct sockaddr_in _address;
		std::vector<char> recv_buffer;
		std::vector<char> send_buffer;
	public:
		ClientSocket();
		~ClientSocket();
};

#endif
