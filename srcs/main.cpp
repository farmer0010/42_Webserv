#include "ServerManager.hpp"
#include "ClientSocket.hpp"
#include "ConfigParser.hpp"

#include <csignal>

int main(int argc, char* argv[])
{
	if (argc != 2) {
		std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
		return 1;
	}
	// 클라이언트가 먼저 끊은 직후 send() 호출 시 SIGPIPE로 프로세스가 종료되는 것을 방지
	std::signal(SIGPIPE, SIG_IGN);
	try {
		ConfigParser parser;
		parser.init();
		Config config = parser.parse();

		ServerManager manager;
		manager.init(config);
		manager.run();
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
