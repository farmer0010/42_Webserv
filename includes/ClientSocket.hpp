#ifndef CLIENTSOCKET_HPP
# define CLIENTSOCKET_HPP

# include <sys/socket.h> // socket, bind, listen, accept, recv, send
# include <netinet/in.h> // sockaddr_in, htons
# include <unistd.h>
# include <fcntl.h>
# include <vector>
# include <iostream>
# include <algorithm> // std::search

# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "ServerSocket.hpp"
# include "Cgi.hpp"

enum ClientState{
	READING,
	REQUEST_COMPLETE,
	CGI_PROCESSING,
	WRITING,
	RESPONSE_COMPLETE
};

class ClientSocket
{
	private:
		int	_client_fd;
		struct sockaddr_in _address;
		//recv로 받은 정보 저장하는 버퍼 => HTTP 요청 메시지 전체를 저장하는 버퍼
		std::vector<char> _recv_buffer;
		//send할 정보 저장하는 버퍼 => HTTP 응답 메시지 전체를 저장하는 버퍼
		std::vector<char> _send_buffer;

		HttpRequest* _request; // HTTP 요청 객체
		HttpResponse* _response; // HTTP 응답 객체
		Cgi* _cgi; // CGI 처리 객체
		ClientState _state;


	public:
		ClientSocket();
		~ClientSocket();

		void init(int client_fd, struct sockaddr_in address, ServerSocket* parent);

		//getter
		int getClientFd() const { return _client_fd; }
		std::vector<char>& getRecvBuffer() { return _recv_buffer; }
		std::vector<char>& getSendBuffer() { return _send_buffer; }
		ClientState getState() const { return _state; }

		//setter
		void setState(ClientState state) { _state = state; }
		void appendToSendBuffer(const char* data, size_t length);
		void appendToRecvBuffer(const char *data, size_t length);

		//checker
		bool isHeaderComplete() const;
		bool isRequestComplete() const;
		bool isResponseComplete() const;
};

#endif
