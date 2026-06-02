#include "ServerManager.hpp"
#include "ClientSocket.hpp"
#include "ConfigParser.hpp"

int main(int argc, char* argv[])
{
	if (argc > 2) {
		std::cerr << "Usage: " << argv[0] << " [config_file]" << std::endl;
		return 1;
	}
	const char* config_path = (argc == 2) ? argv[1] : "conf/default.conf";

	Config config;
	try {
		ConfigParser parser;
		parser.init(config_path);
		config = parser.parse();
	} catch (const std::exception& e) {
		std::cerr << "[Config Error] " << config_path << ": " << e.what() << std::endl;
		return 1;
	}

	try {
		ServerManager manager;
		manager.init(config);
		manager.run();
	} catch (const std::exception& e) {
		std::cerr << "[Runtime Error] " << e.what() << std::endl;
		return 1;
	}
	return 0;
}
