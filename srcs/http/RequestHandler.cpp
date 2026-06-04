#include "RequestHandler.hpp"
#include "Config.hpp"
#include "ConfigParser.hpp"
#include "Cgi.hpp"
#include <iostream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
#include <sstream>
#include <vector>
#include <algorithm>

RequestHandler::RequestHandler() : cgi(NULL), _server_config(NULL) {
}

RequestHandler::~RequestHandler() {
    if (this->cgi) {
        delete this->cgi;
        this->cgi = NULL;
    }
}

void RequestHandler::init(const HttpRequest& req, const ServerBlock* config) {
    this->request = req;
    this->_server_config = config;
    this->response.init();

    if (this->_server_config != NULL) {
        const Location& loc = this->_server_config->getLocationForUri(this->request.getUri());
        std::string relative_uri = this->request.getUri().substr(loc.getPath().length());
        this->absolute_path = loc.getRoot() + relative_uri;

        if (this->request.getMethod() == "GET" && isDirectory(this->absolute_path)) {
            if (this->absolute_path[this->absolute_path.length() - 1] != '/')
                this->absolute_path += "/";
            this->absolute_path += loc.getIndex();
        }
    } else {
        this->absolute_path = "./html" + this->request.getUri();
        if (this->request.getMethod() == "GET" && isDirectory(this->absolute_path)) {
            if (this->absolute_path[this->absolute_path.length() - 1] != '/')
                this->absolute_path += "/";
            this->absolute_path += "index.html";
        }
    }
}

HttpResponse RequestHandler::processRequest() {
    bool is_cgi = false;
    size_t dot_pos = this->absolute_path.find_last_of('.');
    
    if (dot_pos != std::string::npos) {
        std::string ext = this->absolute_path.substr(dot_pos);
        if (ext == ".py" || ext == ".php") {
            is_cgi = true;
        }
    }

    if (is_cgi) {
        if (!isFileExists(this->absolute_path)) {
            generateErrorPage(404);
            return this->response;
        }

        this->cgi = new Cgi(this->request, this->absolute_path);
        
        if (this->cgi->execute() == false) {
            delete this->cgi;
            this->cgi = NULL;
            generateErrorPage(500);
            return this->response;
        }
        return this->response;
    } 
    else {
        if (this->request.getMethod() == "GET") {
            handleGet();
        } else if (this->request.getMethod() == "POST") {
            handlePost();
        } else if (this->request.getMethod() == "DELETE") {
            handleDelete();
        } else {
            generateErrorPage(405);
        }
    }
    return this->response;
}

void RequestHandler::handleCgiResponse(const std::vector<char>& cgi_result) {
    if (cgi_result.empty()) {
        generateErrorPage(500);
        return;
    }

    std::string cgi_str(cgi_result.begin(), cgi_result.end());
    size_t header_end = cgi_str.find("\r\n\r\n");
    size_t delimiter_len = 4;

    if (header_end == std::string::npos) {
        header_end = cgi_str.find("\n\n");
        delimiter_len = 2;
    }

    if (header_end != std::string::npos) {
        std::string cgi_headers = cgi_str.substr(0, header_end);
        std::vector<char> cgi_body(cgi_result.begin() + header_end + delimiter_len, cgi_result.end());

        std::stringstream ss(cgi_headers);
        std::string line;
        while (std::getline(ss, line)) {
            if (!line.empty() && line[line.size() - 1] == '\r')
                line.erase(line.size() - 1);
            if (line.empty()) continue;

            size_t colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);
                
                size_t first = value.find_first_not_of(' ');
                if (first != std::string::npos) {
                    size_t last = value.find_last_not_of(' ');
                    value = value.substr(first, (last - first + 1));
                }

                if (key == "Status") {
                    this->response.setStatusCode(std::atoi(value.substr(0, 3).c_str()));
                    if (value.length() > 4)
                        this->response.setReasonPhrase(value.substr(4));
                } else {
                    this->response.addHeader(key, value);
                }
            }
        }
        this->response.setBody(cgi_body);
    } else {
        this->response.setStatusCode(200);
        this->response.setReasonPhrase("OK");
        this->response.addHeader("Content-Type", "text/plain");
        this->response.setBody(cgi_result);
    }
}

void RequestHandler::handleGet() {
    if (!isFileExists(this->absolute_path)) {
        generateErrorPage(404);
        return;
    }
    std::ifstream file(this->absolute_path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        generateErrorPage(403);
        return;
    }
    std::vector<char> file_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    this->response.addHeader("Content-Type", getMimeType(this->absolute_path));
    this->response.setBody(file_data);
    file.close();
}

void RequestHandler::handlePost() {
    const std::map<std::string, std::string>& req_headers = this->request.getHeaders();
    std::map<std::string, std::string>::const_iterator ct_it = req_headers.find("content-type");
    
    std::string boundary = "";
    if (ct_it != req_headers.end()) {
        std::string ct_value = ct_it->second;
        size_t b_pos = ct_value.find("boundary=");
        if (b_pos != std::string::npos) {
            boundary = ct_value.substr(b_pos + 9);
        }
    }

    const std::vector<char>& request_body = this->request.getBody();
    std::string final_filename = "uploaded_file.bin";
    std::vector<char>::const_iterator file_data_start = request_body.begin();
    std::vector<char>::const_iterator file_data_end = request_body.end();

    if (!boundary.empty() && !request_body.empty()) {
        std::string start_boundary = "--" + boundary;
        std::string end_boundary = "--" + boundary + "--";

        std::vector<char>::const_iterator part_start = std::search(
            request_body.begin(), request_body.end(),
            start_boundary.begin(), start_boundary.end()
        );

        if (part_start != request_body.end()) {
            std::vector<char>::const_iterator search_start = part_start + start_boundary.length();
            const char crlf2[] = "\r\n\r\n";
            std::vector<char>::const_iterator part_header_end = std::search(
                search_start, request_body.end(),
                crlf2, crlf2 + 4
            );

            if (part_header_end != request_body.end()) {
                std::string part_header_str(search_start, part_header_end);
                size_t fn_pos = part_header_str.find("filename=\"");
                if (fn_pos != std::string::npos) {
                    size_t fn_end = part_header_str.find("\"", fn_pos + 10);
                    if (fn_end != std::string::npos) {
                        final_filename = part_header_str.substr(fn_pos + 10, fn_end - (fn_pos + 10));
                    }
                }

                file_data_start = part_header_end + 4;

                std::string delim = "\r\n--" + boundary;
                std::vector<char>::const_iterator part_data_end = std::search(
                    file_data_start, request_body.end(),
                    delim.begin(), delim.end()
                );
                if (part_data_end != request_body.end()) {
                    file_data_end = part_data_end;
                }
            }
        }
    }

    std::string target_path = this->absolute_path;
    if (isDirectory(target_path)) {
        if (target_path[target_path.length() - 1] != '/')
            target_path += "/";
        target_path += final_filename; 
    }

    std::ofstream file(target_path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        generateErrorPage(500);
        return;
    }

    if (file_data_start < file_data_end) {
        file.write(&*file_data_start, file_data_end - file_data_start);
    }
    file.close();

    this->response.setStatusCode(201);
    this->response.setReasonPhrase("Created");
    this->response.setBody("<h1>File Uploaded Successfully!</h1><p>Saved as: " + final_filename + "</p>");
}

void RequestHandler::handleDelete() {
    if (!isFileExists(this->absolute_path)) {
        generateErrorPage(404);
        return;
    }
    if (unlink(this->absolute_path.c_str()) == 0) {
        this->response.setStatusCode(200);
        this->response.setReasonPhrase("OK");
        this->response.setBody("<h1>File Deleted</h1>");
    } else {
        generateErrorPage(403);
    }
}

bool RequestHandler::isDirectory(const std::string& path) {
    struct stat info;
    if (stat(path.c_str(), &info) != 0) return false;
    return S_ISDIR(info.st_mode);
}

bool RequestHandler::isFileExists(const std::string& path) {
    return (access(path.c_str(), F_OK) == 0);
}

std::string RequestHandler::getMimeType(const std::string& path) {
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos) return "application/octet-stream";
    std::string ext = path.substr(dot_pos);
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".txt") return "text/plain";
    return "application/octet-stream"; 
}

void RequestHandler::generateErrorPage(int status_code) {
    this->response.setStatusCode(status_code);
    if (status_code == 404) {
        this->response.setReasonPhrase("Not Found");
        this->response.setBody("<h1>404 Not Found</h1>");
    } else if (status_code == 403) {
        this->response.setReasonPhrase("Forbidden");
        this->response.setBody("<h1>403 Forbidden</h1>");
    } else if (status_code == 405) {
        this->response.setReasonPhrase("Method Not Allowed");
        this->response.setBody("<h1>405 Method Not Allowed</h1>");
    } else if (status_code == 500) {
        this->response.setReasonPhrase("Internal Server Error");
        this->response.setBody("<h1>500 Internal Server Error</h1>");
    }
}

void RequestHandler::clear() {
    this->request.clear();
    this->response.clear();
    this->absolute_path.clear();
    if (this->cgi != NULL) {
        delete this->cgi;
        this->cgi = NULL;
    }
    this->_server_config = NULL;
}