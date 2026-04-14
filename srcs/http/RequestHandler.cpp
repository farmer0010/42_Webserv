#include "RequestHandler.hpp"
#include <iostream>
#include <iterator>

RequestHandler::RequestHandler(){}
RequestHandler::~RequestHandler(){}

void RequestHandler::init(const HttpRequest& req){
    this->request = req;
    this->response.init();

    // 임시 하드코딩 라우팅 (차후에 config 연동시 교체)
    if (this->request.getUri() == "/") {
        this->absolute_path = "./html/index.html";
    } else {
        this->absolute_path = "./html" + this->request.getUri();
    }
}

HttpResponse RequestHandler::processRequest(){
    if(this->request.getMethod() == "GET"){
        handleGet();
    }
    else if(this->request.getMethod() == "POST")
    {
        handlePost();
    }
    else if(this->request.getMethod() == "DELETE"){
        handleDelete();
    }
    else{
        generateErrorPage(405);
    }
    return this->response;
}

void RequestHandler::handleGet(){
    if(!isFileExists(this->absolute_path)){
        generateErrorPage(404);
        return ;
    }

    std::ifstream file(this->absolute_path.c_str(), std::ios::binary);
    if(!file.is_open()){
        generateErrorPage(403); // 파일있지만 열리지않으면 권한 문제
        return;
    }
   std::vector<char> file_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
this->response.setBody(file_data);
file.close();
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
    }
}

void RequestHandler::handlePost() {}
void RequestHandler::handleDelete() {}
bool RequestHandler::isDirectory(const std::string& path) { return false; }