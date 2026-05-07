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

        // 비동기 통신 상태 관리를 위한 변수
        std::vector<char> response_buffer;
        size_t sent_body_size;

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

        ssize_t writeToPipe(); 
        ssize_t readFromPipe(); 

        const std::vector<char>& getResponseBuffer() const { return response_buffer; }
};

#endif