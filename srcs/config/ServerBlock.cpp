#include "ServerBlock.hpp"

ServerBlock::ServerBlock() {}

ServerBlock::~ServerBlock() {}

const Location& ServerBlock::getLocationForuri(const std::string& uri) const {
	const Location* best_match = NULL;
	size_t	max_len = 0;

	for (std::vector<Location>::const_iterator it = _locations.begin(); it != _locations.end(); ++it){
		const std::string& path = it->getPath();
		if (uri.find(path) == 0){
			if (path.length() > max_len){
				max_len = path.length();
				best_match = &(*it);
			}
		}
	}
	if (best_match == NULL)
		throw std::runtime_error("No matching location found for URI: " + uri);
	return *best_match;
}