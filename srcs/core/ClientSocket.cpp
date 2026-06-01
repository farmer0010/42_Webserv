#include "ClientSocket.hpp"

ClientSocket::ClientSocket() : _client_fd(-1), _bytes_sent(0), _state(READING), _last_active_time(0), _cgi(NULL)
{
}

ClientSocket::~ClientSocket()
{
	if (_client_fd >= 0)
		close(_client_fd);
}

void ClientSocket::init(int client_fd, struct sockaddr_in address, ServerSocket* parent)
{
	_client_fd = client_fd;
	_address = address;
	_server_blocks = parent->getServerBlocks();
	_bytes_sent = 0;
	_state = READING;
	_last_active_time = time(NULL);
}

// ─── 헤더 파싱 헬퍼 ──────────────────────────────────────────────────────────

bool ClientSocket::isHeaderComplete() const
{
	const char* crlf = "\r\n\r\n";
	return std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4)
		!= _recv_buffer.end();
}

// recv_buffer의 raw 헤더 영역에서 특정 헤더 값을 추출 (파싱 전 단계에서 사용)
std::string ClientSocket::extractRawHeader(const std::string& key) const
{
	const char* crlf = "\r\n\r\n";
	std::vector<char>::const_iterator header_end =
		std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);

	std::string header_str(_recv_buffer.begin(), header_end);
	std::string search_key = key + ": ";
	size_t pos = header_str.find(search_key);
	if (pos == std::string::npos)
		return "";
	size_t end = header_str.find("\r\n", pos + search_key.size());
	return header_str.substr(pos + search_key.size(), end - pos - search_key.size());
}

// 파싱 전: recv_buffer의 Host 헤더로 서버 블록 선택
const ServerBlock* ClientSocket::selectServerBlockFromBuffer() const
{
	if (_server_blocks.empty())
		return NULL;

	std::string host = extractRawHeader("Host");
	size_t colon = host.find(':');
	if (colon != std::string::npos)
		host = host.substr(0, colon);

	for (size_t i = 0; i < _server_blocks.size(); ++i) {
		if (_server_blocks[i]->getServerName() == host)
			return _server_blocks[i];
	}
	return _server_blocks[0];
}

// 파싱 후: HttpRequest의 Host 헤더로 서버 블록 선택
const ServerBlock* ClientSocket::selectServerBlock() const
{
	if (_server_blocks.empty())
		return NULL;

	const std::map<std::string, std::string>& headers = _request.getHeaders();
	std::map<std::string, std::string>::const_iterator it = headers.find("Host");
	if (it == headers.end())
		return _server_blocks[0];

	std::string host = it->second;
	size_t colon = host.find(':');
	if (colon != std::string::npos)
		host = host.substr(0, colon);

	for (size_t i = 0; i < _server_blocks.size(); ++i) {
		if (_server_blocks[i]->getServerName() == host)
			return _server_blocks[i];
	}
	return _server_blocks[0];
}

// ─── body 크기 제한 ──────────────────────────────────────────────────────────

bool ClientSocket::isRequestComplete() const
{
	const char* crlf = "\r\n\r\n";
	std::vector<char>::const_iterator header_end;

	if (!isHeaderComplete())
		return false;
	//recv 버퍼에서 header 끝 위치 찾기
	header_end = std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);
	//recv 버퍼에서 header가 차지하는 사이즈 계산
	size_t header_size = static_cast<size_t>(header_end - _recv_buffer.begin()) + 4;

	//
	std::string cl_str = extractRawHeader("Content-Length");
	if (cl_str.empty())
		return true; // body 없는 메서드 (GET, DELETE 등)

	size_t content_length = static_cast<size_t>(std::atoi(cl_str.c_str()));
	return (_recv_buffer.size() - header_size) >= content_length;
}

// nginx 방식: Content-Length만 봐도 초과면 즉시 차단, 누적 크기도 체크
bool ClientSocket::isBodyTooLarge() const
{
	const ServerBlock* block = selectServerBlockFromBuffer();
	if (!block)
		return false;

	size_t max_size = block->getClientMaxBodySize();
	if (max_size == 0)
		return false; // 0 = 제한 없음

	// Content-Length 헤더값으로 선제 차단 (body가 오기 전에도 탐지)
	std::string cl_str = extractRawHeader("Content-Length");
	if (!cl_str.empty()) {
		size_t content_length = static_cast<size_t>(std::atoi(cl_str.c_str()));
		if (content_length > max_size)
			return true;
	}

	// 실제 누적된 body 크기 체크 (chunked 등 Content-Length 없는 경우 대비)
	const char* crlf = "\r\n\r\n";
	std::vector<char>::const_iterator header_end =
		std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);
	size_t header_size = static_cast<size_t>(header_end - _recv_buffer.begin()) + 4;
	size_t body_size = (_recv_buffer.size() > header_size) ? _recv_buffer.size() - header_size : 0;

	return body_size > max_size;
}

// ─── 에러 응답 ───────────────────────────────────────────────────────────────

// nginx처럼 413 전송 후 Connection: close로 연결 종료
void ClientSocket::sendErrorResponse(int status_code)
{
	_response.init();
	_response.setVersion("HTTP/1.1");
	_response.setStatusCode(status_code);
	if (status_code == 413)
		_response.setReasonPhrase("Request Entity Too Large");
	_response.addHeader("Connection", "close");
	_response.addHeader("Content-Length", "0");

	std::string resp_str = _response.buildResponse();
	_send_buffer.clear();
	_send_buffer.insert(_send_buffer.end(), resp_str.begin(), resp_str.end());
	_bytes_sent = 0;
	_state = WRITING;
}

// ─── 요청 처리 ───────────────────────────────────────────────────────────────

void ClientSocket::processRequest()
{
	if (!_request.parse(_recv_buffer)) {
		sendErrorResponse(400);
		return;
	}

	// 파싱 완료 후 server_name 기반으로 서버 블록 선택
	// (향후 RequestHandler에 ServerBlock을 전달해 Location 매칭에 활용)
	(void)selectServerBlock();

	_request_handler.init(_request);
	_response = _request_handler.processRequest();

	if (_request_handler.getCgi() != NULL) {
		_state = CGI_WRITING_BODY;
		return;
	}

	std::string resp_str = _response.buildResponse();
	_send_buffer.insert(_send_buffer.end(), resp_str.begin(), resp_str.end());
	_bytes_sent = 0;
	_state = WRITING;
}

// ─── Keep-Alive ──────────────────────────────────────────────────────────────

// HTTP/1.1 기본은 keep-alive, HTTP/1.0 기본은 close
bool ClientSocket::isKeepAlive() const
{
	const std::map<std::string, std::string>& headers = _request.getHeaders();
	std::map<std::string, std::string>::const_iterator it = headers.find("Connection");

	if (it != headers.end())
		return it->second != "close";
	return _request.getVersion() == "HTTP/1.1";
}

// keep-alive 재사용을 위해 요청/응답 상태 초기화, 소켓과 server_blocks는 유지
void ClientSocket::resetForKeepAlive()
{
	_recv_buffer.clear();
	_send_buffer.clear();
	_bytes_sent = 0;
	_request = HttpRequest();
	_response = HttpResponse();
	_last_active_time = time(NULL);
	_state = READING;
}

// ─── epoll 이벤트 핸들러 ─────────────────────────────────────────────────────

// LT 모드: 1회 recv 후 반환, 데이터가 남아 있으면 epoll이 재발화
void ClientSocket::handleRead()
{
	char buf[RECV_CHUNK_SIZE];
	ssize_t n = recv(_client_fd, buf, sizeof(buf), 0);

	if (n < 0) {
		_state = DONE;
		return;
	}
	if (n == 0) {
		_state = DONE; // 클라이언트가 연결 종료
		return;
	}

	_recv_buffer.insert(_recv_buffer.end(), buf, buf + n);
	_last_active_time = time(NULL);

	if (!isHeaderComplete())
		return;

	// 헤더 완성 직후부터 body 크기 제한 적용
	// Content-Length가 max를 넘으면 body가 오기 전에도 413 반환
	if (isBodyTooLarge()) {
		sendErrorResponse(413);
		return;
	}

	if (!isRequestComplete())
		return;

	_state = PROCESSING;
	processRequest();
}

// LT 모드: EPOLLOUT 발화 = 커널 송신 버퍼에 공간 보장
// 1회 send로 충분, EAGAIN 발생 상황이 없으므로 errno 체크 불필요
// 미전송 데이터가 남으면 state = WRITING 유지, LT가 EPOLLOUT 재발화
void ClientSocket::handleWrite()
{
	ssize_t n = send(_client_fd,
					 _send_buffer.data() + _bytes_sent,
					 _send_buffer.size() - _bytes_sent,
					 0);
	if (n < 0) {
		_state = DONE;
		return;
	}
	_bytes_sent += n;

	if (_bytes_sent >= _send_buffer.size()) {
		if (isKeepAlive())
			resetForKeepAlive();
		else
			_state = DONE;
	}
}

// CGI 파이프 fd 접근자 (ServerManager가 epoll 등록에 사용)
int ClientSocket::getCgiWriteFd() const
{
	Cgi* cgi = _request_handler.getCgi();
	return cgi ? cgi->getWriteFd() : -1;
}

int ClientSocket::getCgiReadFd() const
{
	Cgi* cgi = _request_handler.getCgi();
	return cgi ? cgi->getReadFd() : -1;
}

// LT 모드: EPOLLOUT 발화 시 POST body를 CGI stdin에 1회 전송
// writeToPipe() 반환값: >0 = 더 남음, 0 = 전송 완료, <0 = 에러
void ClientSocket::handleCgiWrite()
{
	Cgi* cgi = _request_handler.getCgi();
	if (!cgi) { _state = DONE; return; }

	ssize_t n = cgi->writeToPipe();
	if (n < 0) { _state = DONE; return; }
	if (n == 0)
		_state = CGI_READING_OUTPUT; // body 전송 완료 → 결과 읽기 단계로
}

// LT 모드: EPOLLIN 발화 시 CGI stdout에서 1회 읽기
// readFromPipe() 반환값: >0 = 더 남음, 0 = EOF(CGI 종료), <0 = 에러
void ClientSocket::handleCgiRead()
{
	Cgi* cgi = _request_handler.getCgi();
	if (!cgi) { _state = DONE; return; }

	ssize_t n = cgi->readFromPipe();
	if (n < 0) { _state = DONE; return; }
	if (n == 0) {
		// CGI 프로세스 종료 → 응답 구성
		// handleCgiResponse: CGI 출력을 파싱해 RequestHandler 내부 response 갱신
		_request_handler.handleCgiResponse(cgi->getResponseBuffer());
		// TODO: RequestHandler::getResponse() 추가되면 _response를 갱신해야 함
		std::string resp_str = _response.buildResponse();
		_send_buffer.insert(_send_buffer.end(), resp_str.begin(), resp_str.end());
		_bytes_sent = 0;
		_state = WRITING;
	}
	// n > 0: 아직 읽을 데이터 있음, CGI_READING_OUTPUT 유지, LT 재발화 대기
}
