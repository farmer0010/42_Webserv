#include "ConfigParser.hpp"
#include <set>
#include <sstream>

static bool isNumeric(const std::string& s) {
	if (s.empty()) return false;
	for (size_t i = 0; i < s.size(); ++i)
		if (!isdigit(static_cast<unsigned char>(s[i]))) return false;
	return true;
}

static size_t parseSize(const std::string& s) {
	if (s.empty()) throw std::runtime_error("client_max_body_size: empty value");
	size_t mul = 1;
	std::string num = s;
	char last = s[s.size() - 1];
	if (last == 'k' || last == 'K') { mul = 1024; num = s.substr(0, s.size() - 1); }
	else if (last == 'm' || last == 'M') { mul = 1024 * 1024; num = s.substr(0, s.size() - 1); }
	else if (last == 'g' || last == 'G') { mul = 1024UL * 1024UL * 1024UL; num = s.substr(0, s.size() - 1); }
	if (!isNumeric(num))
		throw std::runtime_error("client_max_body_size: invalid value: " + s);
	return static_cast<size_t>(std::atol(num.c_str())) * mul;
}

static bool isAllowedMethod(const std::string& m) {
	return m == "GET" || m == "POST" || m == "DELETE";
}

static void splitHostPort(const std::string& s, std::string& host, int& port) {
	size_t colon = s.find(':');
	if (colon == std::string::npos) {
		host = "";
		port = std::atoi(s.c_str());
	} else {
		host = s.substr(0, colon);
		port = std::atoi(s.substr(colon + 1).c_str());
	}
}

ConfigParser::ConfigParser() {}
ConfigParser::~ConfigParser() {}

void ConfigParser::init(const std::string& path) {
	this->_file_path = path;
	this->_tokens.clear();
	this->_token_lines.clear();
}

std::string ConfigParser::err(size_t i, const std::string& msg) const {
	size_t line = 0;
	if (i < _token_lines.size()) line = _token_lines[i];
	else if (!_token_lines.empty()) line = _token_lines.back();
	std::ostringstream ss;
	ss << "line " << line << ": " << msg;
	return ss.str();
}

std::string ConfigParser::readFile() {
	std::ifstream ifs(_file_path.c_str());
	if (!ifs.is_open())
		throw std::runtime_error("Cannot open config file: " + _file_path);
	std::stringstream ss;
	ss << ifs.rdbuf();
	return ss.str();
}

void ConfigParser::tokenize(const std::string& content) {
	std::string token;
	size_t content_len = content.length();
	size_t line = 1;
	for (size_t i = 0; i < content_len; i++) {
		char c = content[i];
		if (c == '\n') { line++; continue; }
		if (isspace(c)) continue;
		if (c == '#') {
			while (i < content_len && content[i] != '\n') i++;
			if (i < content_len) line++;
			continue;
		}
		if (c == '{' || c == '}' || c == ';') {
			_tokens.push_back(std::string(1, c));
			_token_lines.push_back(line);
			continue;
		}
		token = "";
		size_t start_line = line;
		while (i < content.length() && !isspace(content[i]) && content[i] != '{' && content[i] != '}' && content[i] != ';' && content[i] != '#') {
			token += content[i];
			i++;
		}
		if (!token.empty()) {
			_tokens.push_back(token);
			_token_lines.push_back(start_line);
		}
		i--;
	}
}

Config ConfigParser::parse() {
    std::string content = readFile();
    tokenize(content);

    Config config;
    size_t i = 0;

    while (i < _tokens.size()) {
        if (_tokens[i] == "server") {
            i++;
            if (i >= _tokens.size() || _tokens[i++] != "{") throw std::runtime_error(err(i, "Expected '{'"));
            
            ServerBlock server;
            server.init();
            std::set<std::string> seen_paths;

            while (i < _tokens.size() && _tokens[i] != "}") {
                if (_tokens[i] == "listen") {
                    i++;
                    std::string host;
                    int port;
                    splitHostPort(_tokens[i++], host, port);
                    if (port < 1 || port > 65535)
                        throw std::runtime_error(err(i, "listen: port out of range (1-65535)"));
                    if (!host.empty()) server.setHost(host);
                    server.setPort(port);
                    if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                }
                else if (_tokens[i] == "host") {
                    i++;
                    server.setHost(_tokens[i++]);
                    if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                }
                else if (_tokens[i] == "server_name") {
                    i++;
                    server.setServerName(_tokens[i++]);
                    if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                }
                else if (_tokens[i] == "client_max_body_size") {
                    i++;
                    size_t sz;
                    try { sz = parseSize(_tokens[i]); }
                    catch (const std::exception& e) { throw std::runtime_error(err(i, e.what())); }
                    server.setClientMaxBodySize(sz);
                    i++;
                    if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                }
                else if (_tokens[i] == "error_page") {
                    i++;
                    std::vector<int> codes;
                    while (i < _tokens.size() && _tokens[i] != ";" && isNumeric(_tokens[i])) {
                        codes.push_back(std::atoi(_tokens[i].c_str()));
                        i++;
                    }
                    if (codes.empty() || i >= _tokens.size() || _tokens[i] == ";")
                        throw std::runtime_error(err(i, "error_page: missing path"));
                    std::string page = _tokens[i++];
                    if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                    for (size_t k = 0; k < codes.size(); ++k)
                        server.addErrorPage(codes[k], page);
                }
                else if (_tokens[i] == "location") {
                    i++;
                    Location loc;
                    loc.init();
                    loc.setPath(_tokens[i++]);
                    if (i >= _tokens.size() || _tokens[i++] != "{") throw std::runtime_error(err(i, "Expected '{'"));
                    
                    while (i < _tokens.size() && _tokens[i] != "}") {
                        if (_tokens[i] == "root") {
                            i++;
                            loc.setRoot(_tokens[i++]);
                            if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                        }
                        else if (_tokens[i] == "index") {
                            i++;
                            loc.setIndex(_tokens[i++]);
                            if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                        }
                        else if (_tokens[i] == "allow_methods") {
                            i++;
                            std::vector<std::string> methods;
                            while (i < _tokens.size() && _tokens[i] != ";") {
                                if (!isAllowedMethod(_tokens[i]))
                                    throw std::runtime_error(err(i, "allow_methods: only GET, POST, DELETE allowed, got: " + _tokens[i]));
                                methods.push_back(_tokens[i++]);
                            }
                            if (i >= _tokens.size()) throw std::runtime_error("Expected ';'");
                            i++;
                            if (methods.empty()) throw std::runtime_error(err(i, "allow_methods: missing values"));
                            loc.setAllowMethods(methods);
                        }
                        else if (_tokens[i] == "autoindex") {
                            i++;
                            std::string val = _tokens[i++];
                            if (val == "on") loc.setAutoindex(true);
                            else if (val == "off") loc.setAutoindex(false);
                            else throw std::runtime_error(err(i, "autoindex: expected 'on' or 'off'"));
                            if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                        }
                        else if (_tokens[i] == "cgi_path") {
                            i++;
                            loc.setCgiPath(_tokens[i++]);
                            if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                        }
                        else if (_tokens[i] == "cgi_extension") {
                            i++;
                            loc.setCgiExtension(_tokens[i++]);
                            if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                        }
                        else if (_tokens[i] == "client_max_body_size") {
                            i++;
                            size_t sz;
                            try { sz = parseSize(_tokens[i]); }
                            catch (const std::exception& e) { throw std::runtime_error(err(i, e.what())); }
                            loc.setClientMaxBodySize(sz);
                            i++;
                            if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                        }
                        else if (_tokens[i] == "return") {
                            i++;
                            loc.setReturnUrl(_tokens[i++]);
                            if (i >= _tokens.size() || _tokens[i++] != ";") throw std::runtime_error(err(i, "Expected ';'"));
                        }
                        else {
                            throw std::runtime_error(err(i, "unknown directive in location: " + _tokens[i]));
                        }
                    }
                    if (!seen_paths.insert(loc.getPath()).second)
                        throw std::runtime_error(err(i, "duplicate location path: " + loc.getPath()));
                    server.addLocation(loc);
                    i++;
                } else {
                    throw std::runtime_error(err(i, "unknown directive in server: " + _tokens[i]));
                }
            }
            config.addServerBlock(server);
            i++;
        } else {
            throw std::runtime_error(err(i, "unknown directive at top level: " + _tokens[i]));
        }
    }

    std::set<std::string> seen_servers;
    const std::vector<ServerBlock>& sbs = config.getServerBlocks();
    for (size_t k = 0; k < sbs.size(); ++k) {
        std::stringstream ss;
        ss << sbs[k].getHost() << ":" << sbs[k].getPort() << ":" << sbs[k].getServerName();
        if (!seen_servers.insert(ss.str()).second)
            throw std::runtime_error("duplicate server (host:port:server_name): " + ss.str());
    }
    return config;
}
