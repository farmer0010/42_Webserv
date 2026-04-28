#ifndef CGI_HPP
# define CGI_HPP

#include "HttpRequest.hpp"
#include <string>
#include <vector>
#include <map>

class Cgi
{
	private:
		HttpRequest request;
		std::string script_path;

		std::map<std::string, std::string> env_map;
		char **envp;

		void initEnv();
		void mapToCharArrays();
		void freeEnv();
		
	public:
		Cgi(const HttpRequest& req, const std::string& path);
		~Cgi();

		std::vector<char> execute();
};
#endif
