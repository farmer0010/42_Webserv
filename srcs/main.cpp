#include "ServerManager.hpp"
#include "ClientSocket.hpp"
#include "ConfigParser.hpp"

int main(int argc, char* argv[])
{
	if (argc != 2) {
		std::cerr << "Usage: " << argv[0] << " <config_file>" << std::endl;
		return 1;
	}
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
