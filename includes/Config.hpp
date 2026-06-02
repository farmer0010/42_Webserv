#ifndef CONFIG_HPP
# define CONFIG_HPP

# include <string>
# include <vector>
# include <map>
# include <iostream>
# include <stdexcept>

class Location
{
	private:
		std::string					_path;
		std::string					_root;
		std::string					_index;
		std::vector<std::string>	_allow_methods;
		bool						_autoindex;
		std::string					_cgi_path;
		std::string					_cgi_extension;
		std::string					_return_url;	
	public:
		Location();
		~Location();
		void init();
		const std::string getPath() const { return _path; }
		void setPath(std::string _path) { this->_path = _path; }
		const std::string getRoot() const { return _root; }
		void setRoot(std::string _root) { this->_root = _root; }
		const std::string& getIndex() const { return _index; }
		void setIndex(std::string _index) { this->_index = _index; }
		const std::vector<std::string>& getAllowMethods() const { return _allow_methods; }
		void setAllowMethods(const std::vector<std::string>& m) { _allow_methods = m; }
		void addAllowMethod(const std::string& m) { _allow_methods.push_back(m); }
		bool getAutoindex() const { return _autoindex; }
		void setAutoindex(bool v) { _autoindex = v; }
		const std::string& getCgiPath() const { return _cgi_path; }
		void setCgiPath(const std::string& p) { _cgi_path = p; }
		const std::string& getCgiExtension() const { return _cgi_extension; }
		void setCgiExtension(const std::string& e) { _cgi_extension = e; }
		const std::string& getReturnUrl() const { return _return_url; }
		void setReturnUrl(const std::string& u) { _return_url = u; }
};

class ServerBlock
{
	private:
		int							_port;
		std::string					_host;
		std::string					_server_name;
		size_t						_client_max_body_size;
		std::map<int, std::string>	_error_pages;
		std::vector<Location>		_locations;
	public:
		ServerBlock();
		~ServerBlock();
		void init();
		const Location& getLocationForUri(const std::string& uri) const;
		void addLocation(const Location& loc) { _locations.push_back(loc); }
		int			getPort() const { return _port; }
		void		setPort(int port) { _port = port; }
		std::string	getHost() const { return _host; }
		void		setHost(const std::string& host) { _host = host; }
		std::string	getServerName() const { return _server_name; }
		void		setServerName(const std::string& name) { _server_name = name; }
		size_t		getClientMaxBodySize() const { return _client_max_body_size; }
		void		setClientMaxBodySize(size_t size) { _client_max_body_size = size; }
		const std::map<int, std::string>& getErrorPages() const { return _error_pages; }
		void		addErrorPage(int code, const std::string& path) { _error_pages[code] = path; }
};

class Config
{
	private:
		std::vector<ServerBlock> _servers;
	public:
		Config();
		~Config();
		void addServerBlock(const ServerBlock& sb) { _servers.push_back(sb); }
		const std::vector<ServerBlock>& getServerBlocks() const { return _servers; }
};

#endif
