#ifndef SERVERSOCKET_HPP
# define SERVERSOCKET_HPP

# include "Config.hpp"

# include <stdexcept>
# include <iostream>
# include <string>
# include <cstring>
# include <sys/socket.h>
# include <netinet/in.h>
# include <unistd.h>
# include <fcntl.h>
# include <netdb.h>
# include <arpa/inet.h>

class ServerSocket
{
	private:
		int	_server_fd;
		struct sockaddr_in _address;
		std::vector<const ServerBlock*> _server_blocks;
	public:
		ServerSocket();                                                                                    // 리스닝 소켓 객체 초기 상태 구성.
		void init(std::string host, int port, const std::vector<const ServerBlock*>& serverBlocks);        // host:port 바인딩 + listen + 논블로킹 + 서버블록 보관.
		~ServerSocket();                                                                                   // 보유 fd 정리.

		int	getFd(){ return this->_server_fd; };                                                           // epoll 등록에 쓰는 listen fd 노출.
		const std::vector<const ServerBlock*>& getServerBlocks() const { return _server_blocks; }          // 이 listen 엔드포인트에 묶인 서버블록 목록 조회.
};

#endif
