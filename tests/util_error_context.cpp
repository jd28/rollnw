#include "nw/util/error_context.hpp"

#include <gtest/gtest.h>

using namespace std::literals;

TEST(ErrorContext, Basics)
{
    EXPECT_EQ(nw::error_context_stack, nullptr);
    ERRARE("This is a test {}", 1);
    EXPECT_NE(nw::error_context_stack, nullptr);
    ERRARE("This is a string test {}", "string"sv);
    auto test = R"([error context] util_error_context.cpp:10     This is a test 1
[error context] util_error_context.cpp:12     This is a string test string
)";
    EXPECT_EQ(nw::get_error_context(), test);
}
