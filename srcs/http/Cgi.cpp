#include "Cgi.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fcntl.h>

Cgi::Cgi(const HttpRequest& req, const std::string& path) 
    : request(req), script_path(path), envp(NULL), pid(-1) {
    pipe_in[0] = -1; pipe_in[1] = -1;
    pipe_out[0] = -1; pipe_out[1] = -1;
    
    initEnv();  // 기록
    mapToCharArrays(); // 기록 내용을 os가 읽는 방식으로 변경
}

Cgi::~Cgi() {
    freeEnv();
    if (pipe_in[1] != -1) close(pipe_in[1]);
    if (pipe_out[0] != -1) close(pipe_out[0]);
}

void Cgi::initEnv() {
    this->env_map["REQUEST_METHOD"] = this->request.getMethod();
    this->env_map["SERVER_PROTOCOL"] = "HTTP/1.1";
    this->env_map["PATH_INFO"] = this->script_path;
    
    std::string full_uri = this->request.getUri();
    size_t question_mark_pos = full_uri.find('?');
    if (question_mark_pos != std::string::npos) {
        this->env_map["QUERY_STRING"] = full_uri.substr(question_mark_pos + 1);
    } else {
        this->env_map["QUERY_STRING"] = "";
    }

    std::map<std::string, std::string> headers = this->request.getHeaders();
    if (headers.count("content-length")) {
        this->env_map["CONTENT_LENGTH"] = headers["content-length"];
    }
    if (headers.count("content-type")) {
        this->env_map["CONTENT_TYPE"] = headers["content-type"];
    }
}

void Cgi::mapToCharArrays() {
    this->envp = new char*[this->env_map.size() + 1];
    int i = 0;
    for (std::map<std::string, std::string>::iterator it = this->env_map.begin(); it != this->env_map.end(); ++it) {
        std::string env_str = it->first + "=" + it->second;
        this->envp[i] = strdup(env_str.c_str());
        i++;
    }
    this->envp[i] = NULL;
}

void Cgi::freeEnv() {
    if (this->envp) {
        for (int i = 0; this->envp[i]; i++) {
            free(this->envp[i]);
        }
        delete[] this->envp;
        this->envp = NULL;
    }
}

bool Cgi::execute() {
    if (pipe(pipe_in) == -1 || pipe(pipe_out) == -1) {
        return false;
    }

    fcntl(pipe_in[1], F_SETFL, O_NONBLOCK);
    fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);

    pid = fork();
    if (pid == -1) {
        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        return false;
    }

    if (pid == 0) {
        close(pipe_in[1]);
        close(pipe_out[0]);

        dup2(pipe_in[0], STDIN_FILENO);
        close(pipe_in[0]);

        dup2(pipe_out[1], STDOUT_FILENO);
        close(pipe_out[1]);

        std::string exec_bin = "/usr/bin/python3";
        size_t dot_pos = this->script_path.find_last_of('.');
        if (dot_pos != std::string::npos) {
            std::string ext = this->script_path.substr(dot_pos);
            if (ext == ".php")
                exec_bin = "/usr/bin/php";
            else if (ext == ".py")
                exec_bin = "/usr/bin/python3";
        }

        char *args[] = {
            (char*)exec_bin.c_str(),
            (char*)this->script_path.c_str(),
            NULL
        };
        execve(args[0], args, this->envp);
        exit(1);
    } else {
        close(pipe_in[0]);
        close(pipe_out[1]);
        return true;
    }
}