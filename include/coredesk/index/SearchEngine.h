#pragma once

#include "coredesk/common/Result.h"
#include "coredesk/common/Types.h"
#include "coredesk/index/IndexSnapshot.h"
#include "coredesk/index/LruCache.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace coredesk::index {

struct SearchRequest {
    std::string query_utf8;
    std::size_t limit{100};
};

struct SearchHit {
    FileId id{};
    int score{};
};

struct SearchResponse {
    IndexGeneration generation{};
    std::vector<SearchHit> hits;
    std::chrono::microseconds elapsed{};
    bool from_cache{false};
};

class SearchEngine {
public:
    explicit SearchEngine(std::size_t cache_capacity = 128);

    Result<SearchResponse> search(const IndexSnapshot& snapshot, const SearchRequest& request);
    void clear_cache();

private:
    struct CachedSearch {
        IndexGeneration generation{};
        std::vector<SearchHit> hits;
    };

    LruCache<std::string, CachedSearch> cache_;
};

} // namespace coredesk::index
