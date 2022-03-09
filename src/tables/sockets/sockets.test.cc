// Copyright (c) 2021 by the Zeek Project. See LICENSE for details.

#include "sockets.h"

#include "autogen/config.h"
#include "util/fmt.h"
#include "util/testing.h"

#include <random>

#ifndef WIN32
#include <netinet/in.h>
#else
#include "util/windows-util.h"
#endif

using namespace zeek::agent;

TEST_CASE_FIXTURE(test::TableFixture, "sockets" * doctest::test_suite("Tables")) {
    useTable("sockets");

    int port = -1;
    int fd = -1;
    std::random_device rd;
    std::uniform_int_distribution<int32_t> dist(1024, 65536);

#ifdef WIN32
    WSADATA wsa{};
    int res = WSAStartup(MAKEWORD(2, 2), &wsa);
    if ( res ) {
        std::error_condition cond = std::system_category().default_error_condition(static_cast<int>(GetLastError()));
        printf("%s", format("Failed to initialize WSA: {}", cond.message()).c_str());
        REQUIRE(res == 0);
    }
#endif

    while ( true ) {
        // Listen on a random port, then check if we can see it.
        fd = socket(AF_INET, SOCK_STREAM, 0);
        REQUIRE(fd >= 0);

#ifndef WIN32
        fchmod(fd, 0777);
#endif

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));

        port = dist(rd);

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(port);
        if ( bind(fd, (const struct sockaddr*)&addr, sizeof(addr)) != 0 )
            // port presumably already in use, try another one
            continue;

#ifndef WIN32
        fchmod(fd, 0777);
#endif
        REQUIRE(listen(fd, SOMAXCONN) >= 0);
        break;
    }

    // We should be able to see our port.
    auto result = query(format("SELECT pid, state from sockets WHERE local_port = {}", port));
    REQUIRE_EQ(result.rows.size(), 1);
    CHECK_EQ(result.get<int64_t>(0, "pid"), getpid());
    CHECK_EQ(result.get<std::string>(0, "state"), std::string("LISTEN"));

    // Clean up
#ifdef WIN32
    closesocket(fd);
    WSACleanup();
#else
    close(fd);
#endif
}
