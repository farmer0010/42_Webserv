#ifndef LOCATION_HPP
# define LOCATION_HPP

# include <string>
# include <vector>

class Location
{
	private:
		std::string		_path;
		std::string		_root;
		std::string		_index;
		std::vector<std::string>	_allow_methods;
		bool			_autoindex;
		std::string		_cgi_path;
		std::string		_cgi_extension;
		std::string		_return_url;	
	public:
		Location();
		~Location();
		void init();
		std::string getPath() const { return _path; }
		std::string getRoot() const { return _root; }
		const std::string& getIndex() const { return _index; }
		void setPath(std::string _path) { this->_path = _path; }
		void setRoot(std::string _root) { this->_root = _root; }
};

#endif
