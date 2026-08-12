#include "coredesk/common/Error.h"
#include "coredesk/common/Result.h"

#include <gtest/gtest.h>

namespace coredesk {
namespace {

TEST(ResultTest, HoldsValue)
{
    auto result = Result<int>::success(42);

    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 42);
}

TEST(ResultTest, HoldsError)
{
    auto result = Result<int>::failure({ErrorCode::InvalidArgument, "bad input"});

    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().message, "bad input");
}

TEST(ResultVoidTest, SupportsSuccessAndFailure)
{
    auto ok = Result<void>::success();
    auto failed = Result<void>::failure({ErrorCode::Cancelled, "cancelled"});

    EXPECT_TRUE(ok.ok());
    EXPECT_FALSE(failed.ok());
    EXPECT_EQ(failed.error().code, ErrorCode::Cancelled);
}

TEST(ErrorTest, ConvertsCodeToString)
{
    EXPECT_EQ(to_string(ErrorCode::HashMismatch), "HashMismatch");
}

} // namespace
} // namespace coredesk
