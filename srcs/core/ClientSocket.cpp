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
