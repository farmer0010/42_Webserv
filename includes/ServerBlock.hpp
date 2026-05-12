#ifndef SEVERBLOCK_HPP
# define SEVERBLOCK_HPP

# include "Location.hpp"
# include "Config.hpp"
# include <stdexcept>

class ServerBlock
{
	private:
		int		_port;
		std::string _host;
		std::string	_server_name;
		size_t		_client_max_body_size;
		std::map<int, std::string> _error_pages;
		std::vector<Location>	_locations;
	public:
		ServerBlock();
		~ServerBlock();
		void init();
		const Location& getLocationForuri(const std::string& uri) const;
};

#endif