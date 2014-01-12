#include "littletest.hpp"
#include <curl/curl.h>
#include <string>
#include <map>
#include "httpserver.hpp"

using namespace httpserver;
using namespace std;

std::string lorem_ipsum(" , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat. , unde omnis iste natus error sit voluptatem accusantium doloremque laudantium, totam rem aperiam eaque ipsa, quae ab illo inventore veritatis et quasi architecto beatae vitae dicta sunt, explicabo. Nemo enim ipsam voluptatem, quia voluptas sit, aspernatur aut odit aut fugit, sed quia consequuntur magni dolores eos, qui ratione voluptatem sequi nesciunt, neque porro quisquam est, qui dolorem ipsum, quia dolor sit, amet, consectetur, adipisci v'elit, sed quia non numquam eius modi tempora incidunt, ut labore et dolore magnam aliquam quaerat voluptatem. Ut enim ad minima veniam, quis nostrum exercitationem ullam corporis suscipit laboriosam, nisi ut aliquid ex ea commodi consequatur? Quis autem vel eum iure reprehenderit, qui in ea voluptate velit esse, quam nihil molestiae consequatur, vel illum, qui dolorem eum fugiat, quo voluptas nulla pariatur? [33] At vero eos et accusamus et iusto odio dignissimos ducimus, qui blanditiis praesentium voluptatum deleniti atque corrupti, quos dolores et quas molestias excepturi sint, obcaecati cupiditate non provident, similique sunt in culpa, qui officia deserunt mollitia animi, id est laborum et dolorum fuga. Et harum quidem rerum facilis est et expedita distinctio. Nam libero tempore, cum soluta nobis est eligendi optio, cumque nihil impedit, quo minus id, quod maxime placeat, facere possimus, omnis voluptas assumenda est, omnis dolor repellendus. Temporibus autem quibusdam et aut officiis debitis aut rerum necessitatibus saepe eveniet, ut et voluptates repudiandae sint et molestiae non recusandae. Itaque earum rerum hic tenetur a sapiente delectus, ut aut reiciendis voluptatibus maiores alias consequatur aut perferendis doloribus asperiores repellat.");


size_t writefunc(void *ptr, size_t size, size_t nmemb, std::string *s)
{
    s->append((char*) ptr, size*nmemb);
    return size*nmemb;
}

size_t headerfunc(void *ptr, size_t size, size_t nmemb, map<string, string>* ss)
{
    string s_ptr((char*)ptr, size*nmemb);
    size_t pos = s_ptr.find(":");
    if(pos != string::npos)
        (*ss)[s_ptr.substr(0, pos)] = 
            s_ptr.substr(pos + 2, s_ptr.size() - pos - 4);
    return size*nmemb;
}

class simple_resource : public http_resource<simple_resource>
{
    public:
        void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_POST(const http_request& req, http_response** res)
        {
            *res = new http_string_response(
                    req.get_arg("arg1")+req.get_arg("arg2"), 200, "text/plain"
            );
        }
};

class long_content_resource : public http_resource<long_content_resource>
{
    public:
        void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response(lorem_ipsum, 200, "text/plain");
        }
};

class header_test_resource : public http_resource<header_test_resource>
{
    public:
        void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
            (*res)->set_header("KEY", "VALUE");
        }
};

class complete_test_resource : public http_resource<complete_test_resource>
{
    public:
        void render_GET(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_POST(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_PUT(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_DELETE(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
        void render_CONNECT(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
};

class only_render_resource : public http_resource<only_render_resource>
{
    public:
        void render(const http_request& req, http_response** res)
        {
            *res = new http_string_response("OK", 200, "text/plain");
        }
};

LT_BEGIN_SUITE(basic_suite)

    webserver* ws;

    void set_up()
    {
        ws = new webserver(create_webserver(8080));
        ws->start(false);
    }

    void tear_down()
    {
        ws->stop();
        delete ws;
    }
LT_END_SUITE(basic_suite)

LT_BEGIN_AUTO_TEST(basic_suite, read_body)
    simple_resource* resource = new simple_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(read_body)

LT_BEGIN_AUTO_TEST(basic_suite, read_long_body)
    long_content_resource* resource = new long_content_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s.size(), lorem_ipsum.size());
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(read_long_body)

LT_BEGIN_AUTO_TEST(basic_suite, read_header)
    header_test_resource* resource = new header_test_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    map<string, string> ss;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headerfunc);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, &ss);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "OK");
    LT_CHECK_EQ(ss["KEY"], "VALUE");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(read_header)

LT_BEGIN_AUTO_TEST(basic_suite, complete)
    complete_test_resource* resource = new complete_test_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(complete)

LT_BEGIN_AUTO_TEST(basic_suite, only_render)
    only_render_resource* resource = new only_render_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL* curl;
    CURLcode res;

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "CONNECT");
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

    curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    curl_easy_cleanup(curl);

LT_END_AUTO_TEST(only_render)

LT_BEGIN_AUTO_TEST(basic_suite, postprocessor)
    simple_resource* resource = new simple_resource();
    ws->register_resource("base", resource);
    curl_global_init(CURL_GLOBAL_ALL);
    std::string s;
    CURL *curl = curl_easy_init();
    CURLcode res;
    curl_easy_setopt(curl, CURLOPT_URL, "localhost:8080/base");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "arg1=lib&arg2=httpserver");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    res = curl_easy_perform(curl);
    LT_ASSERT_EQ(res, 0);
    LT_CHECK_EQ(s, "libhttpserver");
    curl_easy_cleanup(curl);
LT_END_AUTO_TEST(postprocessor)


LT_BEGIN_AUTO_TEST_ENV()
    AUTORUN_TESTS()
LT_END_AUTO_TEST_ENV()
