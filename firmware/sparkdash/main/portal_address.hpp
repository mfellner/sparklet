#pragma once
#ifdef ESP_PLATFORM
#include "lwip/inet.h"
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <cstring>

namespace app {
// IDF's dual-stack HTTP listener reports IPv4 destinations as ::ffff:a.b.c.d.
// Compare the socket's local destination, never a client-supplied Host header.
inline bool local_address_matches(const sockaddr_storage &address, socklen_t length,
                                  in_addr expected) {
    if (address.ss_family == AF_INET && length >= sizeof(sockaddr_in)) {
        sockaddr_in v4{};
        memcpy(&v4, &address, sizeof v4);
        return v4.sin_addr.s_addr == expected.s_addr;
    }
    if (address.ss_family == AF_INET6 && length >= sizeof(sockaddr_in6)) {
        sockaddr_in6 v6{};
        memcpy(&v6, &address, sizeof v6);
        const unsigned char mapped_prefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
        return !memcmp(v6.sin6_addr.s6_addr, mapped_prefix, sizeof mapped_prefix) &&
               !memcmp(v6.sin6_addr.s6_addr + 12, &expected.s_addr, sizeof expected.s_addr);
    }
    return false;
}
} // namespace app
