#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

# include <string>
# include <map>
# include <vector>

class HttpRequest
{
    private:
        std::string method;
        std::string uri;
        std::string version;
        std::map<std::string, std::string> headers;
        std::vector<char> body;
        
    public:
        HttpRequest(){};
        ~HttpRequest(){};

        bool parse(const std::vector<char>& raw_data);
        void clear();

        std::string getMethod() const { return this->method; }
        std::string getUri() const { return this->uri; }
        std::string getVersion() const { return this->version; }
        const std::map<std::string, std::string>& getHeaders() const {
            return this->headers;
        }
        const std::vector<char>& getBody() const { 
            return body; }
};

#endif