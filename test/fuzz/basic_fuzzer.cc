/*
 * Fuzzing test code for libhttpserver using LLVM's libFuzzer
 * (http://llvm.org/docs/LibFuzzer.html)
 * Refer README.md for build instructions
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <iostream>
#include <sstream>
#include <httpserver.hpp>

#define HOST_IP    "127.0.0.1"

unsigned int get_port_no(void);

using namespace httpserver;
unsigned int port;
webserver ws = create_webserver(get_port_no());

class fuzz_resource : public http_resource {
public:
    const std::shared_ptr<http_response> render(const http_request& req) {
        std::stringstream ss;
        req.get_args();
        req.get_headers();
        req.get_footers();
        req.get_cookies();
        req.get_querystring();
        req.get_user();
        req.get_pass();
        req.get_digested_user();
        req.get_requestor();
        req.get_requestor_port();
        for (unsigned int i = 0; i < req.get_path_pieces().size(); i++)
            ss << req.get_path_piece(i) << ",";
        return std::shared_ptr<http_response>(new string_response(ss.str(), 200));
    }
}hwr;

class args_resource: public http_resource {
public:
    const std::shared_ptr<http_response> render(const http_request& req) {
        return std::shared_ptr<http_response>(new string_response("ARGS: " +
                    req.get_arg("arg1") + "and" + req.get_arg("arg2")));
    }
}agr;

unsigned int get_port_no(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in address;
    socklen_t len = sizeof(address);

    memset(&address, 0 ,sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = inet_addr(HOST_IP);
    address.sin_port = 0; //Use the next free port

    bind(fd, (struct sockaddr*) &address, sizeof(address));
    getsockname(fd, (struct sockaddr*) &address, &len);
    port = ntohs(address.sin_port);

    printf("Using port %d\n", port);
    close(fd);
    return port;
}

void quit(const char *msg) {
    perror(msg);
    exit(-1);
}

int connect_server(void) {
    struct sockaddr_in address;
    int sfd, ret;

    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0)
            quit("Failed to open socket");

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = inet_addr(HOST_IP);

retry:
    ret = connect(sfd, (struct sockaddr *)&address, sizeof(address));
    if (ret < 0) {
        if (errno == EINTR)
            goto retry;
        quit("Failed to connect to server");
    }

    return sfd;
}

void write_request(int sfd, const uint8_t *data, size_t size) {
    std::string method = "PUT ";
    std::string suffix = " HTTP/1.1\r\n\r\n";
    std::string str(reinterpret_cast<const char *>(data), size);
    std::string fstr = method + str + suffix;
    const char *msg;
    int bytes, sent = 0;

    size = fstr.length();
    msg = fstr.c_str();
    do {
        bytes = write(sfd, msg + sent, size - sent);
        if (bytes == 0)
            break;
        else if (bytes < 0) {
            if (errno == EINTR)
                continue;
            quit("Failed to write HTTP request");
        }
        sent += bytes;
    } while (sent < size);
}

void read_response(int sfd) {
    char response[150];
    int bytes;

    bytes = read(sfd, response , 150);
    if (bytes < 0)
        return;
#if PRINT_RESPONSE
    printf("%s\n", response);
#endif
}

void cleanup(void)
{
    /* Stop the server */
    ws.stop();
}

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv)
{
    ws.register_resource("{arg1|[A-Z]+}/{arg2|(.*)}", &agr);
    ws.register_resource(R"(.*)", &hwr);

    /* Start the server */
    ws.start(false);

    atexit(cleanup);

    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    int sfd;

    if (memchr(data, '\n', size))
        return 0;

    if (memchr(data, '\r', size))
        return 0;

    /* Client -> connect to server */
    sfd = connect_server();

    /* HTTP request and response*/
    write_request(sfd, data, size);
    read_response(sfd);

    /* Client -> close connection */
    close(sfd);

    return 0;
}
