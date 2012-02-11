#ifndef _STRING_UTILITIES_H_
#define _STRING_UTILITIES_H_

#include <string>
#include <vector>
#include <regex.h>

namespace string_utilities
{

std::string to_upper_copy(std::string str);
std::string to_lower_copy(std::string str);
std::vector<std::string> string_split(std::string s, char sep);
std::string regex_replace(std::string str, std::string pattern, std::string replace_str);

};

#endif
