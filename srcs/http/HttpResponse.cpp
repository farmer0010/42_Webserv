#include "HttpResponse.hpp"
#include <sstream>

HttpResponse::HttpResponse(){}
HttpResponse::~HttpResponse(){}

void HttpResponse::init(){
    this->version = "HTTP/1.1";
    this->status_code = 200;
    this->reason_phrase = "OK";
    this->headers.clear();
    this->body.clear();
}

void HttpResponse::setVersion(const std::string& version){
    this->version = version;
}

void HttpResponse::setStatusCode(int status_code){
    this->status_code = status_code;
}

void HttpResponse::setReasonPhrase(const std::string& reason_phrase){
    this->reason_phrase = reason_phrase;
}

void HttpResponse::addHeader(const std::string& key, const std::string& value){
    this->headers[key] = value;
}

void HttpResponse::setBody(const std::vector<char>& body){
    this->body = body;

    std::stringstream ss;
    ss << this->body.size();
    addHeader("Content-length", ss.str());
}

void HttpResponse::setBody(const std::string& body){
    this->body.assign(body.begin(), body.end());

    std::stringstream ss;
    ss << this->body.size();
    addHeader("content-length", ss.str());
}

std::string HttpResponse::buildResponse() const{
    std::string response;
    std::stringstream ss;

    ss << this->version << " " << this->status_code << " " << this->reason_phrase << "\r\n";
    response = ss.str();

    std::map<std::string, std::string>::const_iterator it;
    for(it = this->headers.begin(); it != this->headers.end(); ++it){
        response += it->first + ": " + it->second + "\r\n";
    }

    response += "\r\n";
    if (!this -> body.empty()){
        response.append(this->body.begin(), this->body.end());
    }
    return response;
}