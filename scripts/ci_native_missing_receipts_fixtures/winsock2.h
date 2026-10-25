// Offline simulator only: never linked into the Windows capture executable.
#ifndef SCRIPTS_LOCAL_FIXTURES_WINSOCK2_H_
#define SCRIPTS_LOCAL_FIXTURES_WINSOCK2_H_
using SOCKET = std::uintptr_t;
using ULONG = unsigned long;
struct WSAPOLLFD { SOCKET fd; short events; short revents; };
constexpr int POLLRDNORM = 1, POLLWRNORM = 2, POLLHUP = 4, POLLERR = 8, POLLNVAL = 16;
constexpr int WSAEINTR = 10004, SOL_SOCKET = 65535, SO_TYPE = 4104;
inline int fake_error = 10038;
inline int fake_poll_error = 10038;
inline int fake_probes = 0;
inline int WSAPoll(WSAPOLLFD* fds, ULONG count, int) {
    for (ULONG i = 0; i < count; ++i) fds[i].revents = POLLNVAL;
    fake_error = fake_poll_error;
    return -1;
}
inline int WSAGetLastError() { return fake_error; }
inline int getsockopt(SOCKET, int, int, char*, int*) {
    ++fake_probes;
    fake_error = 10009;
    return -1;
}
#endif  // SCRIPTS_LOCAL_FIXTURES_WINSOCK2_H_
