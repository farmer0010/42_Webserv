#include "ConfigParser.hpp"

ConfigParser::ConfigParser() {}
ConfigParser::~ConfigParser() {}

void ConfigParser::init() { this->_tokens.clear(); }

void ConfigParser::tokenize(const std::string& content) {
	std::string token;
	size_t content_len = content.length();
	for (size_t i = 0; i < content_len; i++) {
		char c = content[i];
		if (isspace(c)) continue;
		if (c == '{' || c == '}' || c == ';') {
			_tokens.push_back(std::string(1, c));
			continue;
		}
		token = "";
		while (i < content.length() && !isspace(content[i]) && content[i] != '{' && content[i] != '}' && content[i] != ';') {
			token += content[i];
			i++;
		}
		if (!token.empty())
			_tokens.push_back(token);
		i--;
	}
}

Config ConfigParser::parse() {
    Config config;
    size_t i = 0;

    while (i < _tokens.size()) {
        if (_tokens[i] == "server") {
            i++;
            if (i >= _tokens.size() || _tokens[i++] != "{") throw std::runtime_error("Expected '{'");
            
            ServerBlock server;
            server.init();

            while (i < _tokens.size() && _tokens[i] != "}") {
                if (_tokens[i] == "listen") {
                    i++;
                    server.setPort(std::atoi(_tokens[i++].c_str()));
                    if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error("Expected ';'");
                } 
                else if (_tokens[i] == "location") {
                    i++;
                    Location loc;
                    loc.init();
                    loc.setPath(_tokens[i++]);
                    if (i >= _tokens.size() || _tokens[i++] != "{") throw std::runtime_error("Expected '{'");
                    
                    while (i < _tokens.size() && _tokens[i] != "}") {
                        if (_tokens[i] == "root") {
                            i++;
                            loc.setRoot(_tokens[i++]);
                            i++;
                        } else if (_tokens[i] == "index") {
                            i++;
                            loc.setIndex(_tokens[i++]);
                            i++;
                        } else {
                            i++;
                        }
                    }
                    server.addLocation(loc);
                    i++;
                } else {
                    i++;
                }
            }
            config.addServerBlock(server);
            i++;
        } else {
            i++;
        }
    }
    return config;
}
