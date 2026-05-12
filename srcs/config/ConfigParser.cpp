#include "ConfigParser.hpp"

ConfigParser::ConfigParser() {}
ConfigParser::~ConfigParser() {}

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

Config ConfigParser::parse