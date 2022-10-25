#include "test_cellox.hh"

#include <gtest/gtest.h>

TEST(WhileLoops, BlockBody)
{
    test_cellox_program("whileLoops/block_body.clx", "0\n1\n2\n", false);
}

TEST(WhileLoops, ClassInBody)
{
    test_cellox_program("whileLoops/class_in_body.clx", "[line 2] Error at 'class': Expect expression.\n", true);
}

TEST(WhileLoops, EmptyCondition)
{
    test_cellox_program("whileLoops/empty_condition.clx", "[line 1] Error at ')': Expect expression.\n", true);
}

TEST(WhileLoops, SingleExpressionBody)
{
    test_cellox_program("whileLoops/single_expression_body.clx", "1\n2\n3\n", false);
}