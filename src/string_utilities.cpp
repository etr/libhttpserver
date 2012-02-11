#include <algorithm>
#include <string>
#include <istream>
#include <sstream>
#include <vector>
#include <cctype>
#include "string_utilities.hpp"
#include <stdlib.h>
#include <string.h>
 
namespace string_utilities
{

std::string to_upper_copy(std::string str)
{
    std::transform(str.begin(), 
        str.end(), 
        str.begin(),
        (int(*)(int)) std::toupper
    );
    return str;
}
 
std::string to_lower_copy(std::string str)
{
    std::transform(str.begin(), 
        str.end(), 
        str.begin(),
        (int(*)(int)) std::tolower
    );
    return str;
}

std::vector<std::string> string_split(std::string s, char sep)
{
    std::vector<std::string> v;
    std::istringstream buf(s);
    for(std::string token; getline(buf, token, sep); )
    {
        if(token != "")
            v.push_back(token);
    }
    return v;
}

std::string regex_replace(std::string str, std::string pattern, std::string replace_str)
{
    regex_t preg;
    std::string to_ret;
    regmatch_t substmatch[1];
    regcomp(&preg, pattern.c_str(), REG_EXTENDED|REG_ICASE);
    if ( regexec(&preg, str.c_str(), 1, substmatch, 0) == 0 )
    {
        //fprintf(stderr, "%d, %d\n", substmatch[0].rm_so, substmatch[0].rm_eo);
        char *ns = (char*)malloc(substmatch[0].rm_so + 1 + replace_str.size() + (str.size() - substmatch[0].rm_eo) + 2);
        memcpy(ns, str.c_str(), substmatch[0].rm_so+1);
        memcpy(&ns[substmatch[0].rm_so], replace_str.c_str(), replace_str.size());
        memcpy(&ns[substmatch[0].rm_so+replace_str.size()], &str[substmatch[0].rm_eo], strlen(&str[substmatch[0].rm_eo]));
        ns[ substmatch[0].rm_so + replace_str.size() + strlen(&str[substmatch[0].rm_eo]) ] = 0;
        to_ret = std::string(ns);
        free(ns); 
    } 
    regfree(&preg);
    return to_ret;
}

};
