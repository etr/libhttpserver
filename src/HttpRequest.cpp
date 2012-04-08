/*
     This file is part of libhttpserver
     Copyright (C) 2011 Sebastiano Merlino

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/
#include "HttpUtils.hpp"
#include "HttpRequest.hpp"
#include "string_utilities.hpp"

using namespace std;

namespace httpserver
{
//REQUEST
inline HttpRequest::HttpRequest():
	content("")
{
	this->content = "";
}

HttpRequest::HttpRequest(const HttpRequest& o)
{
	this->user = o.user;
	this->pass = o.pass;
	this->path = o.path;
	this->method = o.method;
	this->post_path = o.post_path;
	this->headers = o.headers;
	this->args = o.args;
	this->content = o.content;
}

inline const string HttpRequest::getUser() const 
{
	return this->user;
}

inline const string HttpRequest::getPass() const 
{
	return this->pass;
}

inline void HttpRequest::setUser(const std::string& user) 
{
	this->user = user;
}

inline void HttpRequest::setDigestedUser(const std::string& digestedUser)
{
    this->digestedUser = digestedUser;
}

inline const string HttpRequest::getDigestedUser() const
{
    return this->digestedUser;
}

inline void HttpRequest::setPass(const std::string& pass) 
{
	this->pass = pass;
}

inline const string HttpRequest::getPath() const 
{
	return this->path;
}

inline const vector<string> HttpRequest::getPathPieces() const 
{
	return this->post_path;
}

inline int HttpRequest::getPathPiecesSize() const
{
	return this->post_path.size();
}

inline const string HttpRequest::getPathPiece(int idx) const
{
	if(((int)(this->post_path.size())) > idx)
		return this->post_path[idx];
	return "";
}

inline const string HttpRequest::getMethod() const 
{
	return this->method;
}

inline void HttpRequest::setPath(const string& path)
{
	//this->path = boost::to_lower_copy(path);
	this->path = path;
	vector<string> complete_path = HttpUtils::tokenizeUrl(this->path);
	for(unsigned int i = 0; i < complete_path.size(); i++) 
	{
		this->post_path.push_back(complete_path[i]);
	}
}

inline void HttpRequest::setMethod(const string& method) 
{
	this->method = string_utilities::to_upper_copy(method);
}

inline void HttpRequest::setHeader(const string& key, const string& value) 
{
	this->headers[key] = value;
}

inline void HttpRequest::setFooter(const string& key, const string& value) 
{
	this->footers[key] = value;
}

inline void HttpRequest::setCookie(const string& key, const string& value) 
{
	this->cookies[key] = value;
}

inline void HttpRequest::setVersion(const string& version)
{
	this->version = version;
}

inline const std::string HttpRequest::getVersion() const
{
	return version;
}

inline void HttpRequest::setHeaders(const map<string, string>& headers) 
{
	map<string, string>::const_iterator it;
	for(it = headers.begin(); it != headers.end(); it++)
		this->headers[it->first] = it->second;
}

inline void HttpRequest::setFooters(const map<string, string>& footers) 
{
	map<string, string>::const_iterator it;
	for(it = footers.begin(); it != footers.end(); it++)
		this->footers[it->first] = it->second;
}

inline void HttpRequest::setCookies(const map<string, string>& cookies) 
{
	map<string, string>::const_iterator it;
	for(it = cookies.begin(); it != cookies.end(); it++)
		this->cookies[it->first] = it->second;
}

inline void HttpRequest::setArgs(const map<string, string>& args) 
{
	map<string, string>::const_iterator it;
	for(it = args.begin(); it != args.end(); it++)
		this->args[it->first] = it->second;
}

inline void HttpRequest::setArg(const string& key, const string& value) 
{
	this->args[key] = value;
}

inline void HttpRequest::setArg(const char* key, const char* value, size_t size)
{
	this->args[key] = string(value, size);
}

inline const string HttpRequest::getArg(const string& key) const 
{
	//string new_key = string_utilities::to_lower_copy(key);
	map<string, string>::const_iterator it = this->args.find(key);
	if(it != this->args.end())
		return it->second;
	else
		return "";
}

inline const string HttpRequest::getRequestor() const
{
	return this->requestor;
}

inline void HttpRequest::setRequestor(const std::string& requestor)
{
	this->requestor = requestor;
}

inline short HttpRequest::getRequestorPort() const
{
	return this->requestorPort;
}

inline void HttpRequest::setRequestorPort(short requestorPort)
{
	this->requestorPort = requestorPort;
}

inline const string HttpRequest::getHeader(const string& key) const 
{
	//string new_key = boost::to_lower_copy(key);
	map<string, string>::const_iterator it = this->headers.find(key);
	if(it != this->headers.end())
		return it->second;
	else
		return "";
}

inline const string HttpRequest::getFooter(const string& key) const 
{
	//string new_key = boost::to_lower_copy(key);
	map<string, string>::const_iterator it = this->footers.find(key);
	if(it != this->footers.end())
		return it->second;
	else
		return "";
}

inline const std::vector<std::pair<std::string, std::string> > HttpRequest::getHeaders() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, HeaderComparator>::const_iterator it;
	for(it = headers.begin(); it != headers.end(); it++)
#ifdef USE_CPP_ZEROX
		toRet.push_back(make_pair((*it).first,(*it).second));
#else
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
#endif
	return toRet;
}

inline const std::vector<std::pair<std::string, std::string> > HttpRequest::getCookies() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, HeaderComparator>::const_iterator it;
	for(it = cookies.begin(); it != cookies.end(); it++)
#ifdef USE_CPP_ZEROX
		toRet.push_back(make_pair((*it).first,(*it).second));
#else
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
#endif
	return toRet;
}

inline const std::vector<std::pair<std::string, std::string> > HttpRequest::getFooters() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, HeaderComparator>::const_iterator it;
	for(it = footers.begin(); it != footers.end(); it++)
#ifdef USE_CPP_ZEROX
		toRet.push_back(make_pair((*it).first,(*it).second));
#else
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
#endif
	return toRet;
}

inline const std::vector<std::pair<std::string, std::string> > HttpRequest::getArgs() const
{
	std::vector<std::pair<std::string, std::string> > toRet;
	map<string, string, ArgComparator>::const_iterator it;
	for(it = args.begin(); it != args.end(); it++)
#ifdef USE_CPP_ZEROX
		toRet.push_back(make_pair((*it).first,(*it).second));
#else
		toRet.push_back(make_pair<string, string>((*it).first,(*it).second));
#endif
	return toRet;
}

inline const string HttpRequest::getContent() const
{
	return this->content;
}

inline void HttpRequest::setContent(const string& content) 
{
	this->content = content;
}

inline void HttpRequest::growContent(const char* content, size_t size)
{
	this->content += string(content, size);
}

inline void HttpRequest::removeHeader(const string& key) 
{
	this->headers.erase(key);
}

inline void HttpRequest::set_underlying_connection(struct MHD_Connection* conn)
{
    this->underlying_connection = conn;
}

bool HttpRequest::checkDigestAuth(const std::string& realm, const std::string& password, int nonce_timeout, bool& reloadNonce) const
{
    int val = MHD_digest_auth_check(underlying_connection, realm.c_str(), digestedUser.c_str(), password.c_str(), nonce_timeout);
    if(val == MHD_INVALID_NONCE)
    {
        reloadNonce = true;
        return false;
    }
    else if(val == MHD_NO)
    {
        reloadNonce = false;
        return false;
    }
    reloadNonce = false;
    return true;
}

};
