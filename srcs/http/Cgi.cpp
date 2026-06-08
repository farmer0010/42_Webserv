#include "Cgi.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fcntl.h>
#include <cerrno>
#include <cctype>
#include <sstream>

Cgi::Cgi(const HttpRequest& req, const std::string& path) 
    : request(req), script_path(path), envp(NULL), pid(-1), sent_body_size(0) {
    pipe_in[0] = -1; pipe_in[1] = -1;
    pipe_out[0] = -1; pipe_out[1] = -1;
    
    initEnv(); 
    mapToCharArrays();
}

Cgi::~Cgi() {
    freeEnv();
    if (this->pid > 0) {
        waitpid(this->pid, NULL, WNOHANG);
    }
    if (pipe_in[0] != -1) { 
        close(pipe_in[0]); 
        pipe_in[0] = -1; }
    if (pipe_in[1] != -1) { 
        close(pipe_in[1]); 
        pipe_in[1] = -1; }
    if (pipe_out[0] != -1) {
        close(pipe_out[0]);
        pipe_out[0] = -1; }
    if (pipe_out[1] != -1) { 
        close(pipe_out[1]); 
        pipe_out[1] = -1; }
}

void Cgi::initEnv() {
    std::string uri = this->request.getUri();
    size_t q = uri.find('?');
    std::string path_part = (q == std::string::npos) ? uri : uri.substr(0, q);
    std::string query_part = (q == std::string::npos) ? "" : uri.substr(q + 1);

    this->env_map["GATEWAY_INTERFACE"] = "CGI/1.1";
    this->env_map["SERVER_PROTOCOL"] = "HTTP/1.1";
    this->env_map["SERVER_SOFTWARE"] = "webserv/0.1";
    this->env_map["REQUEST_METHOD"] = this->request.getMethod();
    this->env_map["REQUEST_URI"] = uri;
    this->env_map["SCRIPT_NAME"] = path_part;
    this->env_map["SCRIPT_FILENAME"] = this->script_path;
    this->env_map["PATH_INFO"] = "";
    this->env_map["QUERY_STRING"] = query_part;

    std::string host_val = "";
    const std::map<std::string, std::string>& h = this->request.getHeaders();
    std::map<std::string, std::string>::const_iterator host_it = h.find("host");
    if (host_it != h.end()) {
        host_val = host_it->second;
    }
    std::string server_name = host_val;
    std::string server_port = "80";
    size_t colon = host_val.find(':');
    if (colon != std::string::npos) {
        server_name = host_val.substr(0, colon);
        server_port = host_val.substr(colon + 1);
    }
    this->env_map["SERVER_NAME"] = server_name;
    this->env_map["SERVER_PORT"] = server_port;
    this->env_map["REMOTE_ADDR"] = "127.0.0.1";

    for (std::map<std::string, std::string>::const_iterator it = h.begin(); it != h.end(); ++it) {
        std::string key = "HTTP_" + it->first;
        for (size_t i = 0; i < key.size(); ++i) {
            if (key[i] == '-') {
                key[i] = '_';
            } else {
                key[i] = std::toupper(key[i]);
            }
        }
        if (key == "HTTP_CONTENT_LENGTH" || key == "HTTP_CONTENT_TYPE") {
            continue;
        }
        this->env_map[key] = it->second;
    }

    const std::vector<char>& body = this->request.getBody();
    if (!body.empty()) {
        std::stringstream ss;
        ss << body.size();
        this->env_map["CONTENT_LENGTH"] = ss.str();
    } else {
        this->env_map["CONTENT_LENGTH"] = "0";
    }
    std::map<std::string, std::string>::const_iterator ct_it = h.find("content-type");
    if (ct_it != h.end()) {
        this->env_map["CONTENT_TYPE"] = ct_it->second;
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
    if (pipe(pipe_in) == -1) {
        return false;
    }
    if (pipe(pipe_out) == -1) {
        close(pipe_in[0]); 
        close(pipe_in[1]);
        pipe_in[0] = -1;
        pipe_in[1] = -1;
        return false;
    }

    fcntl(pipe_in[1], F_SETFL, O_NONBLOCK);
    fcntl(pipe_out[0], F_SETFL, O_NONBLOCK);

    pid = fork();
    if (pid == -1) {
        close(pipe_in[0]); close(pipe_in[1]);
        close(pipe_out[0]); close(pipe_out[1]);
        pipe_in[0] = -1; pipe_in[1] = -1;
        pipe_out[0] = -1; pipe_out[1] = -1;
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
        pipe_in[0] = -1;
        pipe_out[1] = -1;
        return true;
    }
}

ssize_t Cgi::writeToPipe() {
    if (pipe_in[1] == -1)
        return 0;
    
    const std::vector<char>& body = this->request.getBody();

    if (body.empty() || sent_body_size >= body.size()) {
        close(pipe_in[1]);
        pipe_in[1] = -1;
        return 0;
    }

    ssize_t bytes = write(pipe_in[1], &body[sent_body_size], body.size() - sent_body_size);
    
    if (bytes > 0) {
        sent_body_size += bytes;
        if (sent_body_size >= body.size()) {
            close(pipe_in[1]);
            pipe_in[1] = -1;
        }
    } 
    else if (bytes == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close(pipe_in[1]);
            pipe_in[1] = -1;
        }
    }
    return bytes;
}

ssize_t Cgi::readFromPipe() {
    if (pipe_out[0] == -1)
        return 0;
    
    char buffer[4096];
	ssize_t bytes = read(pipe_out[0], buffer, sizeof(buffer));

    if (bytes > 0) {
        response_buffer.insert(response_buffer.end(), buffer, buffer + bytes);
    }
    else if (bytes == 0) {
        close(pipe_out[0]);
        pipe_out[0] = -1;
    }
    else if (bytes == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            close(pipe_out[0]);
            pipe_out[0] = -1;
        }
    }
    return bytes;
}