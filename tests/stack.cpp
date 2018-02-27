#include <catch.hpp>

#include "stack.hpp"

TEST_CASE("empty method for empty stack", "")
{
    stack<int> s;
    REQUIRE( s.empty() );
}
