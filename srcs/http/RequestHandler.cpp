#include "RequestHandler.hpp"
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