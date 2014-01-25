#include "binders.hpp"
#include "details/http_response_ptr.hpp"

namespace httpserver
{

namespace details
{

struct modded_request
{
    struct MHD_PostProcessor *pp;
    std::string* complete_uri;
    std::string* standardized_url;
    webserver* ws;

    const binders::functor_two<
        const http_request&, http_response**, void
    > http_resource_mirror::*callback;

    http_request* dhr;
    http_response_ptr dhrs;
    bool second;

    modded_request():
        pp(0x0),
        complete_uri(0x0),
        standardized_url(0x0),
        ws(0x0),
        dhr(0x0),
        dhrs(0x0),
        second(false)
    {
    }
    ~modded_request()
    {
        if (NULL != pp)
        {
            MHD_destroy_post_processor (pp);
        }
        if(second)
            delete dhr; //TODO: verify. It could be an error
        delete complete_uri;
        delete standardized_url;
    }

};

} //details

} //httpserver
