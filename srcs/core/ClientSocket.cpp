#include "ClientSocket.hpp"

ClientSocket::ClientSocket() : _client_fd(-1), _bytes_sent(0), _state(READING), _last_active_time(0)
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

bool ClientSocket::isHeaderComplete() const
{
	const char* crlf = "\r\n\r\n";
	return std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4)
		!= _recv_buffer.end();
}

bool ClientSocket::isRequestComplete() const
{
	if (!isHeaderComplete())
		return false;

	const char* crlf = "\r\n\r\n";
	std::vector<char>::const_iterator header_end =
		std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);
	size_t header_size = static_cast<size_t>(header_end - _recv_buffer.begin()) + 4;

	// 헤더에서 Content-Length 직접 탐색
	std::string header_str(_recv_buffer.begin(), header_end);
	const std::string key = "Content-Length: ";
	size_t pos = header_str.find(key);
	if (pos == std::string::npos)
		return true; // body 없는 메서드 (GET, DELETE 등)

	size_t value_end = header_str.find("\r\n", pos + key.size());
	size_t content_length = static_cast<size_t>(
		std::atoi(header_str.substr(pos + key.size(), value_end - pos - key.size()).c_str()));

	return (_recv_buffer.size() - header_size) >= content_length;
}

// Host 헤더를 서버 블록의 server_name과 매칭, 없으면 첫 번째 블록(default) 반환
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

void ClientSocket::handleRead()
{
	char buf[RECV_BUFFER_SIZE];
	ssize_t n = recv(_client_fd, buf, sizeof(buf), 0);

	if (n <= 0) {
		_state = DONE;
		return;
	}
	_recv_buffer.insert(_recv_buffer.end(), buf, buf + n);
	_last_active_time = time(NULL);

	if (!isRequestComplete())
		return;

	_state = PROCESSING;
	processRequest();
}

void ClientSocket::processRequest()
{
	if (!_request.parse(_recv_buffer)) {
		_state = DONE;
		return;
	}

	// server_name 기반으로 알맞은 서버 블록 선택 (선택 결과는 향후 RequestHandler에 전달 예정)
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

void ClientSocket::handleWrite()
{
	if (_bytes_sent >= _send_buffer.size()) {
		_state = DONE;
		return;
	}

	ssize_t n = send(_client_fd,
					 _send_buffer.data() + _bytes_sent,
					 _send_buffer.size() - _bytes_sent,
					 0);
	if (n < 0) {
		_state = DONE;
		return;
	}
	_bytes_sent += n;

	if (_bytes_sent >= _send_buffer.size())
		_state = DONE;
}

void ClientSocket::handleCgiRead()
{
	// TODO: Cgi 영역과 연동
	_state = PROCESSING_CGI_OUTPUT;
}

void ClientSocket::handleCgiWrite()
{
	// TODO: Cgi 영역과 연동
	_state = CGI_READING_OUTPUT;
}
