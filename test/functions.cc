#include <gtest/gtest.h>
#include "TestUtils.hh"

extern "C"
{
#include "init.h"
}

TEST(Functions, emptyBody)
{
    test_program("functions/emptyBody.clx", "null\n");
}

TEST(Functions, nestedFunction)
{
    test_program("functions/nestedFunction.clx", "0\n1\n1\n2\n3\n5\n8\n13\n21\n34\n");
}

TEST(Functions, parameters)
{
    test_program("functions/parameters.clx", "0\n1\n3\n6\n10\n15\n21\n28\n36\n");
}

TEST(Functions, recursion)
{
    test_program("functions/recursion.clx", "21\n");
}