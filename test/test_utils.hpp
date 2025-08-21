/*
 * Copyright (C) 2024 Jules
 *
 * This file is part of libhttpserver.
 */
#ifndef TEST_TEST_UTILS_HPP_
#define TEST_TEST_UTILS_HPP_

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace test_utils {

int get_random_port() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        return -1;
    }

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;  // bind to a random port

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        close(sock);
        return -1;
    }

    socklen_t len = sizeof(addr);
    if (getsockname(sock, (struct sockaddr *)&addr, &len) == -1) {
        close(sock);
        return -1;
    }

    close(sock);
    return ntohs(addr.sin_port);
}

}  // namespace test_utils

#endif  // TEST_TEST_UTILS_HPP_
