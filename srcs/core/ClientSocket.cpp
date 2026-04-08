#include "ClientSocket.hpp"

ClientSocket::ClientSocket(int client_fd, sockaddr_in address)
{
	this->_client_fd = client_fd;
	this->_address = address;
	this->_state = READING;

	std::cout << "ClientSocket(" << "_client_fd:" << _client_fd << "): constructor called\n";
}

ClientSocket::~ClientSocket() {
    if (_client_fd > 0) {
        close(_client_fd);
    }
}

void ClientSocket::appendToSendBuffer(const char *data, size_t length)
{
	_send_buffer.insert(_send_buffer.end(), data, data + length);
	if (isHeaderComplete()) {
		setState(REQUEST_COMPLETE);
	}
}

void ClientSocket::appendToRecvBuffer(const char *data, size_t length)
{
    _recv_buffer.insert(_recv_buffer.end(), data, data + length);

    if (isHeaderComplete()) {
        setState(REQUEST_COMPLETE);
    }
}

bool ClientSocket::isHeaderComplete() const
{
	const char *crlf = "\r\n\r\n";

	std::vector<char>::const_iterator it = std::search(_recv_buffer.begin(), _recv_buffer.end(), crlf, crlf + 4);

	if (it != _recv_buffer.end()) {
		return true;
	}
	else {
		return false;
	}
}
