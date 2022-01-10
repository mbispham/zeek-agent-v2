// Copyright (c) 2021 by the Zeek Project. See LICENSE for details.

#include "processes.h"

#include "autogen/config.h"
#include "util/testing.h"

using namespace zeek::agent;

TEST_CASE_FIXTURE(test::TableFixture, "processes" * doctest::test_suite("Tables")) {
    useTable("processes");

    // We should be able to see ourselves.
    auto result = query(format("SELECT pid from processes WHERE name = \"zeek-agent\" AND pid = {}", getpid()));
    REQUIRE_EQ(result.rows.size(), 1);
}
