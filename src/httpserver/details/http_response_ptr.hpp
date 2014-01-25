/*
     This file is part of libhttpserver
     Copyright (C) 2011, 2012, 2013, 2014 Sebastiano Merlino

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
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
     USA
*/

#ifndef _HTTP_RESPONSE_PTR_HPP_
#define _HTTP_RESPONSE_PTR_HPP_

namespace httpserver
{

class webserver;

namespace details
{

struct http_response_ptr
{
    public:
        http_response_ptr():
            res(0x0),
            num_references(0x0)
        {
            num_references = new int(0);
        }
        http_response_ptr(http_response* res):
            res(res),
            num_references(0x0)
        {
            num_references = new int(0);
        }
        http_response_ptr(const http_response_ptr& b):
            res(b.res),
            num_references(b.num_references)
        {
            (*num_references)++;
        }
        ~http_response_ptr()
        {
            if(num_references)
            {
                if((*num_references) == 0)
                {
                    if(res && res->autodelete)
                    {
                        delete res;
                        res = 0x0;
                    }
                    delete num_references;
                }
                else
                    (*num_references)--;
            }
        }
        http_response& operator* ()
        {
            return *res;
        }
        http_response* operator-> ()
        {
            return res;
        }
        http_response* ptr()
        {
            return res;
        }
        http_response_ptr& operator= (const http_response_ptr& b)
        {
            if( this != &b)
            {
                if(num_references)
                {
                    if((*num_references) == 0)
                    {
                        if(res && res->autodelete)
                        {
                            delete res;
                            res = 0x0;
                        }
                        delete num_references;
                    }
                    else
                        (*num_references)--;
                }

                res = b.res;
                num_references = b.num_references;
                (*num_references)++;
            }
            return *this;
        }
    private:
        http_response* res;
        int* num_references;
        friend class ::httpserver::webserver;
};

} //details
} //httpserver

#endif //_HTTP_RESPONSE_PTR_HPP_
