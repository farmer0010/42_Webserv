#include "RequestHandler.hpp"
#include "Cgi.hpp"
#include <iostream>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>

RequestHandler::RequestHandler() {}
RequestHandler::~RequestHandler() {}

void RequestHandler::init(const HttpRequest& req) {
    this->request = req;
    this->response.init();

    this->absolute_path = "./html" + this->request.getUri();

    if (isDirectory(this->absolute_path)) {
        if (this->absolute_path[this->absolute_path.length() - 1] != '/')
            this->absolute_path += "/";
        this->absolute_path += "index.html";
    }
}

HttpResponse RequestHandler::processRequest() {
    bool is_cgi = false;
    size_t dot_pos = this -> absolute_path.find_last_of('.');
    
    if (dot_pos != std::string::npos){
        std::string ext = this -> absolute_path.substr(dot_pos);
        if (ext == ".py" || ext == ".php"){
            is_cgi = true;
        }
    }

    if (is_cgi){
        if (!isFileExists(this -> absolute_path)){
            generateErrorPage(404);
            return this -> response;
        }

        Cgi cgi_handler(this -> request, this -> absolute_path);
        std::vector<char> cgi_result = cgi_handler.execute();

        this -> response.setStatusCode(200);
        this -> response.setReasonPhrase("OK");
        this -> response.setBody(cgi_result);
    }
    else{
        if (this->request.getMethod() == "GET") {
            handleGet();
        }
        else if (this->request.getMethod() == "POST") {
            handlePost();
        }
        else if (this->request.getMethod() == "DELETE") {
            handleDelete();
        }
        else {
            generateErrorPage(405);
        }
    }
    return this->response;
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
    std::ofstream file(this->absolute_path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        generateErrorPage(500);
        return;
    }

    const std::vector<char>& request_body = this->request.getBody();
    if (!request_body.empty()) {
        file.write(&request_body[0], request_body.size());
    }
    file.close();

    this->response.setStatusCode(201);
    this->response.setReasonPhrase("Created");
    this->response.setBody("<h1>File Uploaded</h1>");
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
    if (stat(path.c_str(), &info) != 0) {
        return false;
    }
    return S_ISDIR(info.st_mode);
}

bool RequestHandler::isFileExists(const std::string& path) {
    return (access(path.c_str(), F_OK) == 0);
}

std::string RequestHandler::getMimeType(const std::string& path){
    size_t dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos)
        return "application/octet-stream";
    
        std::string ext = path.substr(dot_pos);
        if(ext == ".html" || ext == ".htm")
            return "text/html";
        if(ext == ".css")
            return "text/css";
        if(ext == ".js")
            return "application/javascript";
        if(ext == ".jpg" || ext == ".jpeg")
            return "image/jpeg";
        if(ext == ".png")
            return "image/png";
        if(ext == ".gif")
            return "image/gif";
        if(ext == ".txt")
            return "text/plain";
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