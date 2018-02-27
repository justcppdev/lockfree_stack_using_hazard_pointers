#include <catch.hpp>

#include "stack.hpp"

TEST_CASE("empty method for empty stack", "")
{
    jcd::stack_t<int> s;
    REQUIRE( s.empty() );
}
