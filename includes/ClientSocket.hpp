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
# include "RequestHandler.hpp"
# include "ServerSocket.hpp"
# include "Cgi.hpp"

enum ClientState{
	READING,
	PROCESSING,
	CGI_WRITING_BODY,
	CGI_READING_OUTPUT,
	PROCESSING_CGI_OUTPUT,
	WRITING,
	DONE
};

class ClientSocket
{
	private:
		int	_client_fd;
		struct sockaddr_in _address;
		std::vector<const ServerBlock*> _server_blocks;

		std::vector<char> _recv_buffer;
		std::vector<char> _send_buffer;
		size_t _bytes_sent;

		ClientState _state;
		time_t _last_active_time;

		HttpRequest _request;
		HttpResponse _response;
		RequestHandler _request_handler;
		Cgi _cgi;

	public:
		ClientSocket();
		~ClientSocket();

		void init(int client_fd, struct sockaddr_in address, ServerSocket* parent);

		// 서버매니저가 클라이언트 소켓에서 데이터를 읽을 때 호출
		void handleRead();
		void handleWrite();
		void handleCgiRead();
		void handleCgiWrite();
};

#endif
