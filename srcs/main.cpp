#include "ServerManager.hpp"
#include "ClientSocket.hpp"

int main()
{
	try {
		ServerManager server(8080);
		server.run();
	} catch (const std::exception& e) {
		std::cerr << "Error: " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
