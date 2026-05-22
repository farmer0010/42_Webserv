#ifndef REQUESTHANDLER_HPP
# define REQUESTHANDLER_HPP

# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
# include "Config.hpp"
# include "Cgi.hpp"
# include <string>
# include <fstream>
# include <unistd.h>
# include <sys/stat.h>

class RequestHandler
{
private:
    HttpRequest request;
    HttpResponse response;

    std::string absolute_path;

    Cgi* cgi;

    void handleGet();
    void handlePost();
    void handleDelete();

    bool isDirectory(const std::string& path);
    bool isFileExists(const std::string& path);
    void generateErrorPage(int status_code);

    std::string getMimeType(const std::string& path);

    const ServerBlock* _server_config; //서버 설정 저장
public:
    RequestHandler(); 
    RequestHandler(const ServerBlock& Config); // 생성자에 설정 주입
    ~RequestHandler();

    void init(const HttpRequest& req);

    HttpResponse processRequest();
    void handleCgiResponse(const std::vector<char>& cgi_result);

    Cgi* getCgi() const {return cgi;}
    HttpRequest handleCgiResponse(const HttpRequest& request);
};

#endif