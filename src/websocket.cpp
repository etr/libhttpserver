#include "httpserver/websocket.hpp"

#include <string>
#include <vector>
#include <microhttpd.h>
#include <microhttpd_ws.h>

using namespace httpserver;

void websocket::send(const std::string& message) {
    /* a chat message or command is pending */
    char* frame_data = NULL;
    size_t frame_len = 0;
    int er = MHD_websocket_encode_text (ws,
                                        message.data(),
                                        message.size(),
                                        MHD_WEBSOCKET_FRAGMENTATION_NONE,
                                        &frame_data,
                                        &frame_len,
                                        NULL);
    /* send the data via the TCP/IP socket */
    if (MHD_WEBSOCKET_STATUS_OK == er)
    {
        send_raw(frame_data,
                 frame_len);
    }
    MHD_websocket_free (ws,
                        frame_data);
}

void websocket::send_raw(const char* buf, size_t len) {
    ssize_t ret;
    size_t off;

    for (off = 0; off < len; off += ret)
    {
        ret = ::send(fd,
                     &buf[off],
                     (int) (len - off),
                     0);
        if (0 > ret)
        {
            if (EAGAIN == errno)
            {
                ret = 0;
                continue;
            }
            break;
        }
        if (0 == ret)
        break;
    }
}

void websocket::insert_into_receive_queue(const std::string& message) {
    std::unique_lock<std::mutex> lck(receive_mutex_);
    received_messages_.push_front(message);
}

std::string websocket::receive() {
    std::unique_lock<std::mutex> lck(receive_mutex_);
    while (received_messages_.empty()) receive_cv_.wait(lck);
    std::string result = std::move(received_messages_.back());
    received_messages_.pop_back();
    return result;
}

bool websocket::receive(std::string& message, uint64_t timeout_milliseconds) {
    std::unique_lock<std::mutex> lck(receive_mutex_);
    if (!receive_cv_.wait_for(lck, std::chrono::milliseconds{timeout_milliseconds}, [this](){return !received_messages_.empty();})) {
        return false;
    }
    message = std::move(received_messages_.back());
    received_messages_.pop_back();
    return true;
}

bool websocket::disconnect() const {
    return disconnect_;
}
