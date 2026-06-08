#include "ClientSocket.hpp"

#include <limits>

// 역할: 멤버 fd/카운터/타임스탬프를 0/-1 같은 무효값으로 두고 상태를 READING 으로 시작.
// 책임: 실제 fd/주소/서버블록 주입은 init() 에 위임. 소멸자에서 안전한 close 분기가
//       되도록 "아직 아무 리소스도 보유하지 않음" 을 보장.
ClientSocket::ClientSocket() : _client_fd(-1), _bytes_sent(0), _state(READING), _last_active_time(0), _cgi_start_time(0)
{
}

// 역할: 보유 중인 클라이언트 fd 가 유효하면 close.
// 책임: ServerManager::removeClient 경로에서 객체 해제 시 호출되어, 네트워크 fd 누수가
//       없도록 마지막 안전망 역할만 담당. CGI/요청 자원은 ServerManager/RequestHandler 가 정리.
ClientSocket::~ClientSocket()
{
	if (_client_fd >= 0)
		close(_client_fd);
}

// 역할: accept 직후 ServerManager 가 fd/주소/소속 ServerSocket 을 주입.
// 책임: 새 연결 단위로 상태 머신을 READING 으로 리셋하고, 이 listen 엔드포인트에
//       바인딩된 서버블록 후보 목록을 받아 Host 헤더 매칭의 재료를 마련.
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

// 역할: recv_buffer 안에 헤더 종결 마커("\r\n\r\n") 가 들어왔는지 검사.
// 책임: HttpRequest 파싱을 시도해도 되는 시점인지에 대한 네트워크 레이어 판정.
//       바이트 누적 여부만 보고, 헤더 의미론적 검증은 validateHeaders/HttpRequest 의 몫.
bool ClientSocket::isHeaderComplete() const
{
	const char* crlf = "\r\n\r\n";
	return std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4)
		!= _recv_buffer.end();
}

// 역할: 파싱 전 raw recv_buffer 의 헤더 영역에서 특정 키의 값을 잘라 반환.
// 책임: HttpRequest 가 아직 호출되지 않은 단계(서버블록 선택/Content-Length 검증 등)에서
//       필요한 헤더 한 건을 얻기 위한 임시 추출. 본격 헤더 처리는 HttpRequest 책임.
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

// 역할: raw request-line 에서 URI 토큰만 잘라 반환.
// 책임: HttpRequest 파싱 전 단계에서 location 매칭(예: resolveMaxBodySize) 에 쓰기 위한
//       최소한의 추출. 메서드/버전 검증은 HttpRequest 가 담당.
std::string ClientSocket::extractRawUri() const
{
	const char* crlf = "\r\n";
	std::vector<char>::const_iterator line_end =
		std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 2);
	if (line_end == _recv_buffer.end())
		return "";
	std::string line(_recv_buffer.begin(), line_end);
	size_t sp1 = line.find(' ');
	if (sp1 == std::string::npos) return "";
	size_t sp2 = line.find(' ', sp1 + 1);
	if (sp2 == std::string::npos) return "";
	return line.substr(sp1 + 1, sp2 - sp1 - 1);
}

// 역할: nginx 우선순위(location > server) 로 client_max_body_size 한도를 산출.
// 책임: 한도값 자체의 정의는 Config 레이어 소유, 여기선 buffer 의 현재 상태로
//       서버블록/location 을 골라 "이 요청에 적용할 한도" 를 매핑하는 정책 결정만 담당.
size_t ClientSocket::resolveMaxBodySize() const
{
	const ServerBlock* block = selectServerBlockFromBuffer();
	if (!block)
		return 0;
	try {
		const Location& loc = block->getLocationForUri(extractRawUri());
		size_t loc_size = loc.getClientMaxBodySize();
		if (loc_size != LOCATION_BODY_SIZE_UNSET) return loc_size;
	} catch (...) {}
	return block->getClientMaxBodySize();
}

// 역할: raw 헤더 영역에서 특정 키의 등장 횟수를 센다.
// 책임: RFC 7230 §3.3.3 중복 헤더 검출을 validateHeaders 가 사용할 수 있도록 한 정수만
//       돌려준다. 의미론적 판정(중복=400 등)은 호출자 책임.
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

// 역할: Content-Length 문자열을 안전하게 size_t 로 파싱(공백 트림, 음수/부호/지수 거부, 누적 곱셈 오버플로 검출).
// 책임: 헤더 한 값에 대한 형식/범위 검증의 단일 진실. errno 의존 없이 자체 가드로 끝내,
//       호출자(validateHeaders/isBodyTooLarge/isRequestComplete) 가 일관된 결과로 분기 가능.
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

// 역할: 헤더 완성 직후 RFC 7230 §3.3.3 위반(Content-Length 중복, CL+Transfer-Encoding 동시, 비숫자/오버플로) 검사.
// 책임: HTTP body 길이 결정 직전 마지막 게이트. status code(현재 400) 반환에만 책임지고,
//       응답 구성은 호출자(handleRead → sendErrorResponse) 에 위임.
int ClientSocket::validateHeaders() const
{
	if (countRawHeader("Content-Length") > 1)
		return 400;

	std::string cl_str = extractRawHeader("Content-Length");
	std::string te_str = extractRawHeader("Transfer-Encoding");

	if (!cl_str.empty() && !te_str.empty())
		return 400;

	if (!cl_str.empty()) {
		size_t dummy;
		if (!parseContentLength(cl_str, dummy))
			return 400;
	}
	return 0;
}

// 역할: 파싱 전 단계에서 raw Host 헤더로 서버블록(가상호스트) 매칭.
// 책임: 에러 응답 본문 생성처럼 HttpRequest 호출 전에도 서버블록이 필요한 경로의 단일 입구.
//       매칭 실패 시 첫 블록 fallback 정책도 여기서 결정.
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

// 역할: HttpRequest 파싱 완료 후 정규화된 host 헤더로 서버블록 매칭.
// 책임: 정상 요청 처리 경로(processRequest)에서 RequestHandler 에 전달할 서버블록을 확정.
//       헤더 키는 HttpRequest 가 lowercase 로 저장한다는 계약에 의존.
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

// 역할: Content-Length(선제) + 누적 body(chunked 대비) 두 경로로 한도 초과 여부를 판정.
// 책임: 413 으로 끊을지 말지의 단일 결정점. 한도 산출은 resolveMaxBodySize 위임,
//       응답 구성은 호출자 위임. 한도=0 은 무제한이라는 nginx 관례를 따른다.
bool ClientSocket::isBodyTooLarge() const
{
	size_t max_size = resolveMaxBodySize();
	if (max_size == 0)
		return false;

	std::string cl_str = extractRawHeader("Content-Length");
	if (!cl_str.empty()) {
		size_t content_length = 0;
		if (parseContentLength(cl_str, content_length) && content_length > max_size)
			return true;
	}

	const char* crlf = "\r\n\r\n";
	std::vector<char>::const_iterator header_end =
		std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);
	size_t header_size = static_cast<size_t>(header_end - _recv_buffer.begin()) + 4;
	size_t body_size = (_recv_buffer.size() > header_size) ? _recv_buffer.size() - header_size : 0;

	return body_size > max_size;
}

// 역할: 헤더 완성 + body(또는 chunked 종결 마커) 까지 모두 수신됐는지 판정.
// 책임: processRequest 진입 가능 시점에 대한 네트워크 레이어 결정자. chunked 트레일러
//       의미 해석 자체는 RequestHandler 의 몫이며 여기선 종결 패턴 도달 여부만 본다.
bool ClientSocket::isRequestComplete() const
{
	const char* crlf = "\r\n\r\n";
	std::vector<char>::const_iterator header_end;

	if (!isHeaderComplete())
		return false;
	header_end = std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);
	size_t header_size = static_cast<size_t>(header_end - _recv_buffer.begin()) + 4;

	std::string te = extractRawHeader("Transfer-Encoding");
	for (size_t i = 0; i < te.size(); ++i)
		te[i] = std::tolower(te[i]);
	if (te.find("chunked") != std::string::npos) {
		std::vector<char>::const_iterator body_begin = _recv_buffer.begin() + header_size;
		const char* tail = "\r\n0\r\n\r\n";
		if (std::search(body_begin, _recv_buffer.end(), tail, tail + 7) != _recv_buffer.end())
			return true;
		const char* head_zero = "0\r\n\r\n";
		if (static_cast<size_t>(_recv_buffer.end() - body_begin) >= 5 &&
			std::equal(body_begin, body_begin + 5, head_zero))
			return true;
		return false;
	}

	std::string cl_str = extractRawHeader("Content-Length");
	if (cl_str.empty())
		return true;

	size_t content_length = 0;
	if (!parseContentLength(cl_str, content_length))
		return true;
	return (_recv_buffer.size() - header_size) >= content_length;
}

// 역할: 본문(error_page 매핑)은 RequestHandler::generateErrorPage 에 위임하고, status-line/
//       Connection: close 헤더만 네트워크 레이어에서 보강해 _send_buffer 까지 완성.
// 책임: 에러 응답의 진입 단일점. ServerBlock 선택은 _recv_buffer 의 raw 헤더에 의존하므로
//       반드시 clear 보다 먼저 수행. 본문/Content-Length 는 RequestHandler 와 HttpResponse 의 책임.
void ClientSocket::sendErrorResponse(int status_code)
{
	std::cerr << "[error] " << status_code << " fd=" << _client_fd << std::endl;

	const ServerBlock* sb = selectServerBlockFromBuffer();
	_recv_buffer.clear();

	_request_handler.generateErrorPage(status_code, sb);
	_response = _request_handler.getResponse();

	_response.setVersion("HTTP/1.1");

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

	std::string resp_str = _response.buildResponse();
	_send_buffer.clear();
	_send_buffer.insert(_send_buffer.end(), resp_str.begin(), resp_str.end());
	_bytes_sent = 0;
	_state = WRITING;
}

// 역할: 완성된 요청을 HttpRequest 로 파싱 → 서버블록 선택 → RequestHandler 로 흘려 응답 또는 CGI 분기를 결정.
// 책임: 네트워크 레이어와 HTTP 레이어의 접점. 파싱 실패/HTTP/1.1 Host 누락/서버블록 부재 같은
//       사전 게이트만 여기서 잡고, 라우팅/CGI 실행/응답 본문 생성은 RequestHandler 의 책임.
void ClientSocket::processRequest()
{
	if (!_request.parse(_recv_buffer)) {
		sendErrorResponse(400);
		return;
	}

	std::cout << "[request] " << _request.getMethod() << " "
			  << _request.getUri() << " " << _request.getVersion()
			  << " fd=" << _client_fd << std::endl;

	if (_request.getVersion() == "HTTP/1.1" &&
		_request.getHeaders().find("host") == _request.getHeaders().end()) {
		sendErrorResponse(400);
		return;
	}

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

// 역할: HTTP 버전 기본값 + Connection 헤더로 keep-alive 여부 결정.
// 책임: 응답 송신 완료 시점의 분기점만 제공. 실제 상태 초기화는 resetForKeepAlive 가 담당.
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

// 역할: 같은 연결에서 다음 요청을 받기 위해 buffer/요청/응답/핸들러 상태를 리셋.
// 책임: 소켓 fd 와 _server_blocks 같은 "연결 단위" 자원은 유지하고, "요청 단위" 자원만
//       초기화. RequestHandler::clear 호출로 이전 요청의 CGI 포인터/내부 상태가 다음
//       사이클에 누수되지 않도록 보장하는 것도 여기 책임.
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

// 역할: EPOLLIN 발화 시 1회 recv → 누적 → (헤더 완성 시) 검증/한도 검사 → (요청 완성 시) processRequest 트리거.
// 책임: 네트워크 수신의 단일 입구. LT 모드 가정 하에 잔여 EPOLLIN 은 다음 사이클로 미루고,
//       READING 외 상태에서는 동일 요청 중복 처리 방지를 위해 즉시 반환.
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
		_state = DONE;
		return;
	}

	_recv_buffer.insert(_recv_buffer.end(), buf, buf + n);
	_last_active_time = time(NULL);

	if (!isHeaderComplete())
		return;

	int header_err = validateHeaders();
	if (header_err != 0) {
		sendErrorResponse(header_err);
		return;
	}

	if (isBodyTooLarge()) {
		sendErrorResponse(413);
		return;
	}

	if (!isRequestComplete())
		return;

	_state = PROCESSING;
	processRequest();
}

// 역할: EPOLLOUT 발화 시 _send_buffer 의 미전송 구간을 1회 send → 송신 완료 시 keep-alive/close 분기.
// 책임: 네트워크 송신의 단일 입구. errno 검사 금지 룰에 따라 n<0 은 즉시 DONE 으로 종료시키며,
//       부분 전송은 _bytes_sent 누적으로 LT 재발화에 맡긴다.
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

// 역할: ServerManager 가 CGI 파이프를 epoll 에 등록할 수 있도록 write 파이프 fd 를 노출.
// 책임: 파이프 fd 의 소유/수명은 Cgi 가 가지며, 여기선 핸들러를 통해 단순 조회만 한다.
int ClientSocket::getCgiWriteFd() const
{
	Cgi* cgi = _request_handler.getCgi();
	return cgi ? cgi->getWriteFd() : -1;
}

// 역할: read 파이프 fd 노출.
// 책임: getCgiWriteFd 와 동일한 단순 조회 책임.
int ClientSocket::getCgiReadFd() const
{
	Cgi* cgi = _request_handler.getCgi();
	return cgi ? cgi->getReadFd() : -1;
}

// 역할: CGI 자식 PID 노출 (없으면 -1).
// 책임: ServerManager 의 SIGKILL/waitpid 경로가 자식을 안전하게 회수할 수 있도록 PID 만 전달.
pid_t ClientSocket::getCgiPid() const
{
	Cgi* cgi = _request_handler.getCgi();
	return cgi ? cgi->getPid() : -1;
}

// 역할: CGI stdin 파이프 EPOLLOUT 발화 시 POST body 를 1회 전송, 0 반환 시 read 단계로 전이.
// 책임: 파이프 I/O 자체는 Cgi 가 수행하고, 여기선 상태 머신 전이와 활동 시각 갱신만 담당.
//       n<0 시 errno 구분 불가 룰 때문에 즉시 DONE 으로 끊지 않고 LT 재발화에 맡긴다.
void ClientSocket::handleCgiWrite()
{
	Cgi* cgi = _request_handler.getCgi();
	if (!cgi) { _state = DONE; return; }

	ssize_t n = cgi->writeToPipe();
	if (n < 0) return;
	_last_active_time = time(NULL);
	if (n == 0)
		_state = CGI_READING_OUTPUT;
}

// 역할: CGI stdout 파이프 EPOLLIN 발화 시 1회 읽기, EOF(=0) 시 RequestHandler 로 응답 합성을 위임 후 WRITING 으로 전이.
// 책임: 파이프 read 자체는 Cgi 가 담당하고, 여기선 EOF 시점에 RequestHandler::handleCgiResponse 를
//       불러 _response 를 채우고 _send_buffer 까지 직렬화하는 흐름만 조립.
void ClientSocket::handleCgiRead()
{
	Cgi* cgi = _request_handler.getCgi();
	if (!cgi) { _state = DONE; return; }

	ssize_t n = cgi->readFromPipe();
	if (n < 0) return;
	_last_active_time = time(NULL);
	if (n == 0) {
		std::cout << "[cgi] done pid=" << cgi->getPid()
				  << " fd=" << _client_fd << std::endl;
		_request_handler.handleCgiResponse(cgi->getResponseBuffer());
		_response = _request_handler.getResponse();
		std::string resp_str = _response.buildResponse();
		_send_buffer.insert(_send_buffer.end(), resp_str.begin(), resp_str.end());
		_bytes_sent = 0;
		_state = WRITING;
	}
}
