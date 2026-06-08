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
	READING,                // 요청 수신 단계
	PROCESSING,             // 요청 파싱/처리 진행 중
	CGI_WRITING_BODY,       // CGI stdin 으로 body 전송 중
	CGI_READING_OUTPUT,     // CGI stdout 에서 결과 수신 중
	PROCESSING_CGI_OUTPUT,  // 수신한 CGI 출력 후처리 (현재 미사용)
	WRITING,                // 클라이언트로 응답 송신 중
	DONE                    // 정리 대상 (close 예정)
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
		time_t _cgi_start_time;

		HttpRequest		_request;
		HttpResponse	_response;
		RequestHandler	_request_handler;

		std::string			extractRawHeader(const std::string& key) const;        // 파싱 전 raw 버퍼에서 특정 헤더 값 추출.
		std::string			extractRawUri() const;                                 // 파싱 전 raw request-line 에서 URI 추출.
		size_t				resolveMaxBodySize() const;                            // location > server 우선순위로 body 한도 결정.
		size_t				countRawHeader(const std::string& key) const;          // raw 헤더 영역에서 특정 키 등장 횟수.
		bool				parseContentLength(const std::string& cl_str, size_t& out) const; // Content-Length 안전 파싱(공백/오버플로 가드).
		int					validateHeaders() const;                               // RFC 7230 §3.3.3 위반 조합 검사(0=OK, status code 반환).
		const ServerBlock*	selectServerBlockFromBuffer() const;                   // 파싱 전 Host 헤더 기반 서버블록 선택.
		const ServerBlock*	selectServerBlock() const;                             // 파싱 후 Host 헤더 기반 서버블록 선택.

		bool				isBodyTooLarge() const;                                // Content-Length 또는 누적 body 가 한도 초과인지.
		void				sendErrorResponse(int status_code);                    // RequestHandler 에 본문 위임 + 헤더 보강하여 에러 응답 구성.
		void				processRequest();                                      // 완성된 요청을 RequestHandler 로 흘려 응답/CGI 분기.
		bool				isRequestComplete() const;                             // 헤더+body(또는 chunked 종결) 까지 모두 수신됐는지.
		bool				isKeepAlive() const;                                   // 응답 후 연결을 유지할지(HTTP 버전/Connection 헤더 기준).
		void				resetForKeepAlive();                                   // 다음 요청을 받기 위해 요청/응답 상태 초기화.

	public:
		ClientSocket();                                                            // fd=-1, state=READING 으로 초기 상태 구성.
		~ClientSocket();                                                           // 보유 fd close.

		void		init(int client_fd, struct sockaddr_in address, ServerSocket* parent); // accept 직후 fd/주소/서버블록 목록 주입.

		int			getFd() const { return _client_fd; }                           // epoll 등록에 쓰는 클라이언트 fd.
		ClientState	getState() const { return _state; }                            // 현재 상태 머신 값(분기/타이머 sweep 용).
		int			getCgiWriteFd() const;                                         // CGI stdin 파이프 fd (없으면 -1).
		int			getCgiReadFd() const;                                          // CGI stdout 파이프 fd (없으면 -1).
		pid_t		getCgiPid() const;                                             // CGI 자식 PID (없으면 -1).
		time_t		getLastActiveTime() const { return _last_active_time; }        // 마지막 read/write 시각(idle 만료 판정용).
		time_t		getCgiStartTime() const { return _cgi_start_time; }            // CGI 시작 시각(CGI 만료 판정용, 0 = 비진행).

		void		handleRead();                                                  // EPOLLIN: recv → 누적 → 완성 시 processRequest.
		void		handleWrite();                                                 // EPOLLOUT: send → 송신 완료 시 keep-alive/close 분기.
		void		handleCgiRead();                                               // CGI stdout EPOLLIN: 결과 누적 → EOF 시 응답 구성.
		void		handleCgiWrite();                                              // CGI stdin EPOLLOUT: body 전송 → 완료 시 read 단계로.

		bool		isHeaderComplete() const;                                      // recv_buffer 에 "\r\n\r\n" 가 들어왔는지.
};

#endif
