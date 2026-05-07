#ifndef CGI_HPP
# define CGI_HPP

#include "HttpRequest.hpp"
#include <string>
#include <vector>
#include <map>
#include <fcntl.h>

class Cgi
{
	private:
		HttpRequest request;
		std::string script_path;

		std::map<std::string, std::string> env_map;
		char **envp;

		int pipe_in[2];
		int pipe_out[2];
		pid_t pid;

		void initEnv();
		void mapToCharArrays();
		void freeEnv();
		
	public:
		Cgi(const HttpRequest& req, const std::string& path);
		~Cgi();

		bool execute();

		int getWriteFd() const { return pipe_in[1]; }
		int getReadFd() const { return pipe_out[0]; }
		pid_t getPid() const { return pid; }
};

#endif