#include "../../firmware/sparkdash/main/portal_address.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

#include <unistd.h>

int main() {
    in_addr expected{};
    assert(inet_pton(AF_INET, "192.168.4.1", &expected) == 1);
    sockaddr_storage storage{};
    sockaddr_in v4{};
    v4.sin_family = AF_INET;
    v4.sin_addr = expected;
    memcpy(&storage, &v4, sizeof v4);
    assert(app::local_address_matches(storage, sizeof v4, expected));
    assert(!app::local_address_matches(storage, sizeof v4 - 1, expected));
    assert(inet_pton(AF_INET, "192.168.1.20", &v4.sin_addr) == 1);
    memcpy(&storage, &v4, sizeof v4);
    assert(!app::local_address_matches(storage, sizeof v4, expected));

    sockaddr_in6 v6{};
    v6.sin6_family = AF_INET6;
    for (const char *address :
         {"::ffff:192.168.4.1", "::ffff:192.168.1.20", "::192.168.4.1", "::1", "::"}) {
        assert(inet_pton(AF_INET6, address, &v6.sin6_addr) == 1);
        memcpy(&storage, &v6, sizeof v6);
        assert(app::local_address_matches(storage, sizeof v6, expected) ==
               (strcmp(address, "::ffff:192.168.4.1") == 0));
        assert(!app::local_address_matches(storage, sizeof v4, expected));
    }

    // Exercise a real dual-stack socket, reproducing the HTTP listener's address shape.
    int listener = socket(AF_INET6, SOCK_STREAM, 0);
    assert(listener >= 0);
    int off = 0;
    assert(setsockopt(listener, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof off) == 0);
    v6 = {};
    v6.sin6_family = AF_INET6;
    assert(bind(listener, reinterpret_cast<sockaddr *>(&v6), sizeof v6) == 0);
    assert(listen(listener, 1) == 0);
    socklen_t size = sizeof v6;
    assert(getsockname(listener, reinterpret_cast<sockaddr *>(&v6), &size) == 0);
    int client = socket(AF_INET, SOCK_STREAM, 0);
    assert(client >= 0);
    v4.sin_port = v6.sin6_port;
    assert(inet_pton(AF_INET, "127.0.0.1", &v4.sin_addr) == 1);
    assert(connect(client, reinterpret_cast<sockaddr *>(&v4), sizeof v4) == 0);
    int accepted = accept(listener, nullptr, nullptr);
    assert(accepted >= 0);
    size = sizeof storage;
    assert(getsockname(accepted, reinterpret_cast<sockaddr *>(&storage), &size) == 0);
    assert(storage.ss_family == AF_INET6);
    assert(app::local_address_matches(storage, size, v4.sin_addr));
    assert(!app::local_address_matches(storage, size, expected));
    close(accepted);
    close(client);
    close(listener);
    puts("Portal address checks passed, including a real dual-stack connection");
}
