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
	WRITING,
};

class ClientSocket
{
	private:
		int	_client_fd;
		struct sockaddr_in _address;
		std::vector<char> _recv_buffer;
		std::vector<char> _send_buffer;
		ClientState _state;
	public:
		ClientSocket(int client_fd, struct sockaddr_in address);
		~ClientSocket();

		//getter
		int getFd() const { return _client_fd; }
		std::vector<char>& getRecvBuffer() { return _recv_buffer; }
		std::vector<char>& getSendBuffer() { return _send_buffer; }
		ClientState getState() const { return _state; }

		//setter
		void setState(ClientState state) { _state = state; }
};

#endif
