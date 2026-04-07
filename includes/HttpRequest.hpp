#ifndef HTTPREQUEST_HPP
# define HTTPREQUEST_HPP

#include <string>

class HttpRequest
{
	private:
		std::string method;
		std::string uri;
		std::string version;
	public:
		HttpRequest();
		~HttpRequest();
};

#endif
