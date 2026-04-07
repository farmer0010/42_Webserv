#ifndef HTTPRESPONSE_HPP
# define HTTPRESPONSE_HPP

#include <string>

class HttpResponse
{
	private:
		std::string version;
		std::string status_code;
		std::string reason_phrase;
	public:
		HttpResponse();
		~HttpResponse();
};

#endif
