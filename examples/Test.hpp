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

#ifndef _TEST_HPP_
#define _TEST_HPP_
#include <httpserver.hpp>

using namespace httpserver;

class Test : public http_resource<Test> {
	public:
        Test();
		void render_GET(const http_request&, http_response**);
        void render_PUT(const http_request&, http_response**);
        void render_POST(const http_request&, http_response**);
};

class Test2 : public http_resource<Test2> {
	public:
        Test2();
        void render_GET(const http_request&, http_response**);
};

#endif
