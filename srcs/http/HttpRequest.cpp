#include "HttpRequest.hpp"
#include <iostream>
#include <algorithm>
#include <cstdlib>

bool HttpRequest::parse(const std::vector<char>& raw_data) {
    const char crlf2[] = "\r\n\r\n";
    std::vector<char>::const_iterator header_end = std::search(raw_data.begin(), raw_data.end(), crlf2, crlf2 + 4);
    
    if (header_end == raw_data.end()) {
        return false;
    }

    std::string header_str(raw_data.begin(), header_end);
    
    size_t line_end = header_str.find("\r\n");
    if (line_end == std::string::npos) return false;
    std::string start_line = header_str.substr(0, line_end);
    
    size_t first_space = start_line.find(" ");
    if (first_space == std::string::npos) return false;
    this->method = start_line.substr(0, first_space);

    size_t second_space = start_line.find(" ", first_space + 1);
    if (second_space == std::string::npos) return false;
    this->uri = start_line.substr(first_space + 1, second_space - (first_space + 1));
    this->version = start_line.substr(second_space + 1);

    size_t pos = line_end + 2;
    while (pos < header_str.length()) {
        size_t next_crlf = header_str.find("\r\n", pos);
        if (next_crlf == std::string::npos) break;
        
        std::string line = header_str.substr(pos, next_crlf - pos);
        if (line.empty()) break;

        size_t colon_pos = line.find(":");
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 1);
            if (!value.empty() && value[0] == ' ') value.erase(0, 1);
            
            for (size_t i = 0; i < key.length(); ++i) {
                key[i] = std::tolower(key[i]);
            }
            
            this->headers[key] = value;
        }
        pos = next_crlf + 2;
    }

    std::vector<char>::const_iterator body_start = header_end + 4;
    
    if (this->headers.find("content-length") != this->headers.end()) {
        int content_length = std::atoi(this->headers["content-length"].c_str());
        
        int available_body_size = raw_data.end() - body_start;
        int copy_size = (available_body_size < content_length) ? available_body_size : content_length;
        
        this->body.assign(body_start, body_start + copy_size);
    } 
    
    return true;
}