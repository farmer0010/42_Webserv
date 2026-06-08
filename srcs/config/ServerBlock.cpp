#include "Config.hpp"

Config::Config() {}
Config::~Config() {}

Location::Location() {}
Location::~Location() {}

void Location::init() {
	this->_path = "";
	this->_root = "";
	this->_index = "index.html";
	this->_allow_methods.clear();
	this->_allow_methods.push_back("GET");
	this->_autoindex = false;
	this->_cgi_path = "";
	this->_cgi_extension = "";
	this->_return_url = "";
	this->_client_max_body_size = LOCATION_BODY_SIZE_UNSET;
}

ServerBlock::ServerBlock() {}
ServerBlock::~ServerBlock() {}

void ServerBlock::init() {
	this->_port = 80;
	this->_host = "0.0.0.0";
	this->_server_name = "";
	this->_client_max_body_size = 1048576;
	this->_error_pages.clear();
	this->_locations.clear();
}

const Location& ServerBlock::getLocationForUri(const std::string& uri) const {
	const Location* best_match = NULL;
	size_t	max_len = 0;

	for (std::vector<Location>::const_iterator it = _locations.begin(); it != _locations.end(); ++it){
		const std::string& path = it->getPath();
		if (uri.find(path) == 0){
			if (path.length() > max_len){
				max_len = path.length();
				best_match = &(*it);
			}
		}
	}
	if (best_match == NULL)
		throw std::runtime_error("No matching location found for URI: " + uri);
	return *best_match;
}