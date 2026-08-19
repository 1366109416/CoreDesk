#include "coredesk/index/LruCache.h"

#include <gtest/gtest.h>

#include <string>

namespace coredesk::index {
namespace {

TEST(LruCacheTest, GetMissReturnsNullopt)
{
    LruCache<std::string, int> cache(2);
    EXPECT_FALSE(cache.get("missing").has_value());
}

TEST(LruCacheTest, PutThenGetReturnsValue)
{
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    ASSERT_TRUE(cache.get("a").has_value());
    EXPECT_EQ(*cache.get("a"), 1);
}

TEST(LruCacheTest, HitMovesEntryToMostRecentlyUsed)
{
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    EXPECT_TRUE(cache.get("a").has_value());
    cache.put("c", 3);

    EXPECT_TRUE(cache.get("a").has_value());
    EXPECT_FALSE(cache.get("b").has_value());
    EXPECT_TRUE(cache.get("c").has_value());
}

TEST(LruCacheTest, UpdatesExistingValue)
{
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("a", 10);
    ASSERT_TRUE(cache.get("a").has_value());
    EXPECT_EQ(*cache.get("a"), 10);
    EXPECT_EQ(cache.size(), 1U);
}

TEST(LruCacheTest, EvictsLeastRecentlyUsed)
{
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.put("b", 2);
    cache.put("c", 3);
    EXPECT_FALSE(cache.get("a").has_value());
    EXPECT_TRUE(cache.get("b").has_value());
    EXPECT_TRUE(cache.get("c").has_value());
}

TEST(LruCacheTest, CapacityOneKeepsOnlyNewest)
{
    LruCache<std::string, int> cache(1);
    cache.put("a", 1);
    cache.put("b", 2);
    EXPECT_FALSE(cache.get("a").has_value());
    EXPECT_TRUE(cache.get("b").has_value());
}

TEST(LruCacheTest, DefaultCapacityIsOneHundredTwentyEight)
{
    LruCache<std::string, int> cache;
    EXPECT_EQ(cache.capacity(), 128U);
}

TEST(LruCacheTest, ClearRemovesEntries)
{
    LruCache<std::string, int> cache(2);
    cache.put("a", 1);
    cache.clear();
    EXPECT_FALSE(cache.get("a").has_value());
    EXPECT_EQ(cache.size(), 0U);
}

} // namespace
} // namespace coredesk::index
