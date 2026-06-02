#ifndef CONFIGPARSER_HPP
# define CONFIGPARSER_HPP

# include <string>
# include <vector>
# include <fstream>
# include <sstream>
# include <iostream>
# include <stdexcept>
# include <cstdlib>
# include "Config.hpp"

class ConfigParser
{
	private:
		std::string _file_path;
		std::vector<std::string> _tokens;
		std::string readFile();
		void tokenize(const std::string& content);
	public:
		ConfigParser();
		~ConfigParser();
		void init(const std::string& path);
		Config parse();
};

#endif