#include "ClientSocket.hpp"

#include <limits>

ClientSocket::ClientSocket() : _client_fd(-1), _bytes_sent(0), _state(READING), _last_active_time(0), _cgi_start_time(0)
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
	_cgi_start_time = 0;
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

// raw 헤더 영역에서 특정 키가 등장한 횟수 (RFC 7230 §3.3.3 중복 검출용).
// request-line 의 메서드가 우연히 "Content-Length:" 같은 문자열을 가질 수 없으므로
// 헤더 영역 전체에서 "\r\n" + key + ":" 패턴을 카운트해도 안전.
size_t ClientSocket::countRawHeader(const std::string& key) const
{
	const char* crlf = "\r\n\r\n";
	std::vector<char>::const_iterator header_end =
		std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);
	std::string header_str(_recv_buffer.begin(), header_end);

	std::string needle = "\r\n" + key + ":";
	size_t count = 0;
	size_t pos = 0;
	while ((pos = header_str.find(needle, pos)) != std::string::npos) {
		count++;
		pos += needle.size();
	}
	return count;
}

// Content-Length 값을 검증하며 size_t 로 파싱.
// - 앞뒤 공백/탭 허용 (RFC 7230 OWS)
// - 모든 문자가 ASCII digit 이어야 함 (음수/부호/지수 모두 거부)
// - 누적 곱셈에서 자체 오버플로 검출 (errno 사용 없이 size_t max 비교)
// 반환: true 성공 (out 채워짐), false 실패
bool ClientSocket::parseContentLength(const std::string& cl_str, size_t& out) const
{
	if (cl_str.empty())
		return false;
	size_t start = 0, end = cl_str.size();
	while (start < end && (cl_str[start] == ' ' || cl_str[start] == '\t')) start++;
	while (end > start && (cl_str[end - 1] == ' ' || cl_str[end - 1] == '\t')) end--;
	if (start == end)
		return false;

	const size_t max_size = std::numeric_limits<size_t>::max();
	size_t val = 0;
	for (size_t i = start; i < end; ++i) {
		if (cl_str[i] < '0' || cl_str[i] > '9')
			return false;
		size_t digit = static_cast<size_t>(cl_str[i] - '0');
		if (val > (max_size - digit) / 10)
			return false;
		val = val * 10 + digit;
	}
	out = val;
	return true;
}

// 헤더 완성 직후 호출. RFC 7230 §3.3.3 위반 조합/값을 검사.
// 반환: 0 = OK, 그 외 = HTTP status code (현재 400만 사용)
int ClientSocket::validateHeaders() const
{
	// Content-Length 중복 → 400
	if (countRawHeader("Content-Length") > 1)
		return 400;

	std::string cl_str = extractRawHeader("Content-Length");
	std::string te_str = extractRawHeader("Transfer-Encoding");

	// Content-Length + Transfer-Encoding 동시 → 400 (request smuggling 방지)
	if (!cl_str.empty() && !te_str.empty())
		return 400;

	// Content-Length 값이 유효 size_t 인지
	if (!cl_str.empty()) {
		size_t dummy;
		if (!parseContentLength(cl_str, dummy))
			return 400;
	}
	return 0;
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
	std::map<std::string, std::string>::const_iterator it = headers.find("host");
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

	// Transfer-Encoding: chunked 인 경우 종결 마커 "0\r\n\r\n" 까지 받아야 완료
	// (Content-Length가 없어도 즉시 완료로 판정해서 body가 일부만 RequestHandler/CGI로
	//  넘어가던 문제 차단)
	std::string te = extractRawHeader("Transfer-Encoding");
	for (size_t i = 0; i < te.size(); ++i)
		te[i] = std::tolower(te[i]);
	if (te.find("chunked") != std::string::npos) {
		std::vector<char>::const_iterator body_begin = _recv_buffer.begin() + header_size;
		const char* tail = "\r\n0\r\n\r\n";
		if (std::search(body_begin, _recv_buffer.end(), tail, tail + 7) != _recv_buffer.end())
			return true;
		// 첫 청크가 즉시 0인 케이스 (희귀): body 시작이 "0\r\n\r\n"
		const char* head_zero = "0\r\n\r\n";
		if (static_cast<size_t>(_recv_buffer.end() - body_begin) >= 5 &&
			std::equal(body_begin, body_begin + 5, head_zero))
			return true;
		return false;
	}

	std::string cl_str = extractRawHeader("Content-Length");
	if (cl_str.empty())
		return true; // body 없는 메서드 (GET, DELETE 등)

	// validateHeaders 가 미리 통과시킨 값이라 실패 케이스는 도달하지 않음.
	// 방어적으로 실패 시 완료로 처리하여 무한 대기 방지.
	size_t content_length = 0;
	if (!parseContentLength(cl_str, content_length))
		return true;
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
		size_t content_length = 0;
		// 파싱 실패면 validateHeaders 가 이미 400 처리했거나 처리할 예정 → 여기선 false
		if (parseContentLength(cl_str, content_length) && content_length > max_size)
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

// nginx처럼 에러 응답 후 Connection: close로 연결 종료
// 에러 시점에 _recv_buffer에 쌓인 잔여 요청 데이터는 더 이상 처리하지 않으므로
// 메모리 점유 방지를 위해 비움
void ClientSocket::sendErrorResponse(int status_code)
{
	std::cerr << "[error] " << status_code << " fd=" << _client_fd << std::endl;
	_recv_buffer.clear();

	_response.init();
	_response.setVersion("HTTP/1.1");
	_response.setStatusCode(status_code);

	const char* phrase = "";
	switch (status_code) {
		case 400: phrase = "Bad Request"; break;
		case 403: phrase = "Forbidden"; break;
		case 404: phrase = "Not Found"; break;
		case 405: phrase = "Method Not Allowed"; break;
		case 413: phrase = "Request Entity Too Large"; break;
		case 500: phrase = "Internal Server Error"; break;
		case 501: phrase = "Not Implemented"; break;
		case 505: phrase = "HTTP Version Not Supported"; break;
	}
	_response.setReasonPhrase(phrase);

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

	std::cout << "[request] " << _request.getMethod() << " "
			  << _request.getUri() << " " << _request.getVersion()
			  << " fd=" << _client_fd << std::endl;

	// HTTP/1.1은 Host 헤더 필수 (RFC 7230 §5.4). 없으면 400.
	// 헤더 키는 HttpRequest::parse가 lowercase로 저장하므로 "host"로 조회.
	if (_request.getVersion() == "HTTP/1.1" &&
		_request.getHeaders().find("host") == _request.getHeaders().end()) {
		sendErrorResponse(400);
		return;
	}

	// 파싱 완료 후 server_name 기반으로 서버 블록 선택, RequestHandler에 전달
	const ServerBlock* sb = selectServerBlock();
	if (!sb) {
		sendErrorResponse(500);
		return;
	}

	_request_handler.init(_request, sb);
	_response = _request_handler.processRequest();

	if (_request_handler.getCgi() != NULL) {
		_cgi_start_time = time(NULL);
		std::cout << "[cgi] spawn pid=" << _request_handler.getCgi()->getPid()
				  << " fd=" << _client_fd << std::endl;
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
	std::map<std::string, std::string>::const_iterator it = headers.find("connection");

	if (it != headers.end()) {
		std::string value = it->second;
		for (size_t i = 0; i < value.size(); ++i)
			value[i] = std::tolower(value[i]);
		return value != "close";
	}
	return _request.getVersion() == "HTTP/1.1";
}

// keep-alive 재사용을 위해 요청/응답 상태 초기화, 소켓과 server_blocks는 유지
// _request_handler도 재생성: 이전 요청의 cgi 포인터/내부 상태가 남아 다음
// CGI 요청에서 누수/dangling pipe fd로 이어지는 것을 차단
void ClientSocket::resetForKeepAlive()
{
	_recv_buffer.clear();
	_send_buffer.clear();
	_bytes_sent = 0;
	_request.clear();
	_response.clear();
	_request_handler.clear();
	_last_active_time = time(NULL);
	_cgi_start_time = 0;
	_state = READING;
}

// ─── epoll 이벤트 핸들러 ─────────────────────────────────────────────────────

// LT 모드: 1회 recv 후 반환, 데이터가 남아 있으면 epoll이 재발화
// PROCESSING/CGI_*/WRITING 상태에서 EPOLLIN이 잔여 발화되어도 무시
// (요청 처리 중 추가 read를 막아 동일 요청 중복 처리 방지)
void ClientSocket::handleRead()
{
	if (_state != READING)
		return;
	char buf[RECV_CHUNK_SIZE];
	ssize_t n = recv(_client_fd, buf, sizeof(buf), 0);

	if (n < 0) {
		_state = DONE;
		return;
	}
	if (n == 0) {
		std::cout << "[peer-closed] fd=" << _client_fd << std::endl;
		_state = DONE; // 클라이언트가 연결 종료
		return;
	}

	_recv_buffer.insert(_recv_buffer.end(), buf, buf + n);
	_last_active_time = time(NULL);

	if (!isHeaderComplete())
		return;

	// 헤더 완성 직후 RFC 7230 §3.3.3 위반 조합/Content-Length 값 검증
	// (중복 CL / CL+Transfer-Encoding 공존 / 비숫자·오버플로 → 400)
	int header_err = validateHeaders();
	if (header_err != 0) {
		sendErrorResponse(header_err);
		return;
	}

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
	_last_active_time = time(NULL);

	if (_bytes_sent >= _send_buffer.size()) {
		std::cout << "[response] sent " << _send_buffer.size()
				  << " bytes fd=" << _client_fd
				  << (isKeepAlive() ? " (keep-alive)" : " (close)") << std::endl;
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

pid_t ClientSocket::getCgiPid() const
{
	Cgi* cgi = _request_handler.getCgi();
	return cgi ? cgi->getPid() : -1;
}

// LT 모드: EPOLLOUT 발화 시 POST body를 CGI stdin에 1회 전송
// writeToPipe() 반환값: >0 = 더 남음, 0 = 전송 완료, <0 = 에러
void ClientSocket::handleCgiWrite()
{
	Cgi* cgi = _request_handler.getCgi();
	if (!cgi) { _state = DONE; return; }

	ssize_t n = cgi->writeToPipe();
	if (n < 0) { _state = DONE; return; }
	_last_active_time = time(NULL);
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
	_last_active_time = time(NULL);
	if (n == 0) {
		// CGI 프로세스 종료 → 응답 구성
		// handleCgiResponse가 RequestHandler 내부 response를 채우면
		// getResponse()로 가져와 _response에 반영해야 buildResponse가 CGI 결과를 직렬화
		std::cout << "[cgi] done pid=" << cgi->getPid()
				  << " fd=" << _client_fd << std::endl;
		_request_handler.handleCgiResponse(cgi->getResponseBuffer());
		_response = _request_handler.getResponse();
		std::string resp_str = _response.buildResponse();
		_send_buffer.insert(_send_buffer.end(), resp_str.begin(), resp_str.end());
		_bytes_sent = 0;
		_state = WRITING;
	}
	// n > 0: 아직 읽을 데이터 있음, CGI_READING_OUTPUT 유지, LT 재발화 대기
}
