#ifndef REQUESTHANDLER_HPP
# define REQUESTHANDLER_HPP

# include "HttpRequest.hpp"
# include "HttpResponse.hpp"
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

    void handleGet();
    void handlePost();
    void handleDelete();

    bool isDirectory(const std::string& path);
    bool isFileExists(const std::string& path);
    void generateErrorPage(int status_code);

    std::string getMimeType(const std::string& path);

public:
    RequestHandler();
    ~RequestHandler();

    void init(const HttpRequest& req);

    HttpResponse processRequest();
    void handleCgiResponse(const std::vector<char>& cgi_result);
};

#endif