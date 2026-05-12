#ifndef CONFIG_HPP
# define CONFIG_HPP

# include <string>
# include <vector>
# include <map>
# include "Location.hpp"

class Config
{
	private:
		int		_listen_port;
		std::string _server_name;
		std::map<int, std::string> _error_pages;
		std::vector<Location> _locations;
	public:
		Config();
		~Config();
		void init();
		int getListenPort() const { return _listen_port; }
		void setListPort(int _listen_port) { this->_listen_port = _listen_port; }
		void addLocation(const Location& loc) { _locations.push_back(loc); }
};

#endif
