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
