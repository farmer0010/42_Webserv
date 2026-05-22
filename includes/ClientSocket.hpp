#ifndef CLIENTSOCKET_HPP
# define CLIENTSOCKET_HPP

# include <sys/socket.h>
# include <netinet/in.h>
# include <unistd.h>
# include <fcntl.h>
# include <ctime>
# include <cstdlib>
# include <vector>
# include <string>
# include <map>
# include <algorithm>
# include <iostream>

# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "RequestHandler.hpp"
# include "ServerSocket.hpp"
# include "Cgi.hpp"

# define RECV_CHUNK_SIZE 8192

enum ClientState {
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
		int _client_fd;
		struct sockaddr_in _address;
		std::vector<const ServerBlock*> _server_blocks;

		std::vector<char> _recv_buffer;
		std::vector<char> _send_buffer;
		size_t _bytes_sent;

		ClientState _state;
		time_t _last_active_time;

		HttpRequest _request;
		HttpResponse		_response;
		RequestHandler		_request_handler;
		Cgi					_cgi;

		// recv_buffer 파싱 전 단계에서 raw 헤더 값 추출
		std::string			extractRawHeader(const std::string& key) const;
		// 파싱 전 단계에서 server block 선택 (Host 헤더 기반)
		const ServerBlock*	selectServerBlockFromBuffer() const;
		// 파싱 완료 후 server block 선택 (HttpRequest 헤더 기반)
		const ServerBlock*	selectServerBlock() const;

		bool				isBodyTooLarge() const;
		void				sendErrorResponse(int status_code);
		void				processRequest();
		bool				isRequestComplete() const;
		bool				isKeepAlive() const;
		void				resetForKeepAlive();

	public:
		ClientSocket();
		~ClientSocket();

		void		init(int client_fd, struct sockaddr_in address, ServerSocket* parent);

		int			getFd() const { return _client_fd; }
		ClientState	getState() const { return _state; }

		void		handleRead();
		void		handleWrite();
		void		handleCgiRead();
		void		handleCgiWrite();

		bool		isHeaderComplete() const;
};

#endif
