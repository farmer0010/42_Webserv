#include "HttpRequest.hpp"
#include <iostream>

bool HttpRequest::parse(const std::vector<char>& raw_data){
    std::string request_str(raw_data.begin(), raw_data.end());

    size_t line_end = request_str.find("\r\n");
    if (line_end == std::string::npos) {
        std::cerr << "에러: 잘못된 http 요청입니다(crlf 없음)." << std::endl;
        return false;
    }
    std::string start_line = request_str.substr(0, line_end);

    size_t first_space = start_line.find(" ");
    if (first_space == std::string::npos) 
        return false;
    this->method = start_line.substr(0, first_space);

    size_t second_space = start_line.find(" ", first_space + 1);
    if (second_space == std::string::npos) 
        return false;
    this->uri = start_line.substr(first_space + 1, second_space - (first_space + 1));

    this->version = start_line.substr(second_space + 1);

    // \r\n 두 글자 건너 뛴 위치 선정
    size_t pos = line_end + 2;
    while(pos < request_str.length()){
        size_t next_crlf = request_str.find("\r\n", pos);
        if (next_crlf == std::string::npos)
            return false;
        std::string line = request_str.substr(pos, next_crlf - pos);

        if (line.empty()){
            pos = next_crlf + 2;
            break;
        }

        size_t colon_pos = line.find(":");
        if (colon_pos != std::string::npos){
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos+1);
            if (!value.empty() && value[0] == ' ')
            {
                value.erase(0,1);
            }
            this->headers[key] = value;
        }
        pos = next_crlf+2;
    }
    this->body.insert(this->body.end(), raw_data.begin() + pos, raw_data.end());

    return true;
}