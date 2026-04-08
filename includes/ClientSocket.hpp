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
		int	_client_fd; // 클라이언트 소켓 파일 디스크립터
		struct sockaddr_in _address; // 클라이언트 주소 정보
		std::vector<char> _recv_buffer; //
		std::vector<char> _send_buffer; //
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
		void appendToSendBuffer(const char* data, size_t length) {
			_send_buffer.insert(_send_buffer.end(), data, data + length);
		}
};

#endif
