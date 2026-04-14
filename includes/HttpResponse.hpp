#ifndef HTTPRESPONSE_HPP
# define HTTPRESPONSE_HPP

# include <string>
# include <map>
# include <vector>

class HttpResponse
{
    private:
        std::string version;
        int status_code;
        std::string reason_phrase;

        std::map<std::string, std::string> headers;
        std::vector<char> body;

    public:
        HttpResponse();
        ~HttpResponse();

		void init();

        void setVersion(const std::string& version);
        void setStatusCode(int status_code);
        void setReasonPhrase(const std::string& reason_phrase);
        
        void addHeader(const std::string& key, const std::string& value);
        
        void setBody(const std::vector<char>& body);
        void setBody(const std::string& body);

        std::string buildResponse() const;
};

#endif