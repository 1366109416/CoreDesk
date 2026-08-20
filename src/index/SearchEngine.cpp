#include "coredesk/index/SearchEngine.h"

#include "coredesk/index/Tokenizer.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

namespace coredesk::index {
namespace {

constexpr std::size_t kMaxQueryBytes = 256;
constexpr std::size_t kMaxLimit = 100;

struct RankedHit {
    SearchHit hit;
    std::size_t file_name_length{};
    std::string normalized_relative_path;
};

bool starts_with(std::string_view value, std::string_view prefix) noexcept
{
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

std::size_t clamp_limit(std::size_t limit) noexcept
{
    if (limit == 0 || limit > kMaxLimit) {
        return kMaxLimit;
    }
    return limit;
}

std::string make_cache_key(std::string_view normalized_query, std::size_t limit)
{
    std::string key(normalized_query);
    key.push_back('\n');
    key += std::to_string(limit);
    return key;
}

std::vector<FileId> merge_prefix_postings(const IndexSnapshot& snapshot, std::string_view token)
{
    std::vector<FileId> merged;
    auto it = std::lower_bound(snapshot.sorted_tokens.begin(), snapshot.sorted_tokens.end(), token);
    for (; it != snapshot.sorted_tokens.end() && starts_with(*it, token); ++it) {
        const auto posting = snapshot.token_index.find(*it);
        if (posting != snapshot.token_index.end()) {
            merged.insert(merged.end(), posting->second.ids.begin(), posting->second.ids.end());
        }
    }

    std::sort(merged.begin(), merged.end());
    merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
    return merged;
}

std::vector<FileId> candidates_for_token(const IndexSnapshot& snapshot, std::string_view token)
{
    const auto exact = snapshot.token_index.find(std::string(token));
    if (exact != snapshot.token_index.end()) {
        return exact->second.ids;
    }
    return merge_prefix_postings(snapshot, token);
}

std::vector<FileId> intersect_sorted(const std::vector<FileId>& left, const std::vector<FileId>& right)
{
    std::vector<FileId> result;
    result.reserve(std::min(left.size(), right.size()));
    std::set_intersection(left.begin(), left.end(), right.begin(), right.end(), std::back_inserter(result));
    return result;
}

std::string normalized_file_name(const filesystem::FileRecord& record)
{
    return normalize_index_text(path_to_index_text(record.file_name));
}

std::string normalized_relative_path(const filesystem::FileRecord& record)
{
    return normalize_index_text(path_to_index_text(record.relative_path));
}

std::unordered_set<std::string> record_tokens(const filesystem::FileRecord& record)
{
    std::unordered_set<std::string> tokens;
    for (auto& token : tokenize_path(record.file_name)) {
        tokens.insert(std::move(token));
    }
    for (auto& token : tokenize_path(record.extension)) {
        tokens.insert(std::move(token));
    }
    for (auto& token : tokenize_path(record.relative_path)) {
        tokens.insert(std::move(token));
    }
    return tokens;
}

int score_index_hit(const filesystem::FileRecord& record,
                    std::string_view normalized_file_name,
                    std::string_view normalized_query,
                    const std::vector<std::string>& query_tokens)
{
    if (normalized_file_name == normalized_query) {
        return 100;
    }
    if (!normalized_query.empty() && starts_with(normalized_file_name, normalized_query)) {
        return 80;
    }

    const auto tokens = record_tokens(record);
    for (const auto& query_token : query_tokens) {
        if (tokens.find(query_token) != tokens.end()) {
            return 60;
        }
    }

    for (const auto& query_token : query_tokens) {
        if (std::any_of(tokens.begin(), tokens.end(), [&](const std::string& token) {
                return starts_with(token, query_token);
            })) {
            return 50;
        }
    }

    return 30;
}

bool ranked_hit_less(const RankedHit& left, const RankedHit& right)
{
    if (left.hit.score != right.hit.score) {
        return left.hit.score > right.hit.score;
    }

    if (left.file_name_length != right.file_name_length) {
        return left.file_name_length < right.file_name_length;
    }

    return left.normalized_relative_path < right.normalized_relative_path;
}

RankedHit make_ranked_hit(const IndexSnapshot& snapshot,
                          std::size_t pos,
                          std::string_view normalized_query,
                          const std::vector<std::string>& query_tokens,
                          int fallback_score = 0)
{
    const auto& record = snapshot.records[pos];
    const auto file_name = normalized_file_name(record);
    const auto score = fallback_score == 0
        ? score_index_hit(record, file_name, normalized_query, query_tokens)
        : fallback_score;
    return RankedHit{
        SearchHit{record.id, score},
        file_name.size(),
        normalized_relative_path(record),
    };
}

std::vector<SearchHit> top_hits(std::vector<RankedHit> ranked, std::size_t limit)
{
    if (ranked.size() > limit) {
        std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::vector<RankedHit>::difference_type>(limit), ranked.end(), ranked_hit_less);
        ranked.resize(limit);
    } else {
        std::sort(ranked.begin(), ranked.end(), ranked_hit_less);
    }

    std::vector<SearchHit> hits;
    hits.reserve(ranked.size());
    for (const auto& hit : ranked) {
        hits.push_back(hit.hit);
    }
    return hits;
}

std::vector<SearchHit> build_index_hits(const IndexSnapshot& snapshot,
                                        const std::vector<FileId>& candidates,
                                        std::string_view normalized_query,
                                        const std::vector<std::string>& query_tokens,
                                        std::size_t limit)
{
    std::vector<RankedHit> ranked;
    ranked.reserve(candidates.size());
    for (const auto id : candidates) {
        const auto pos = snapshot.id_to_pos.find(id);
        if (pos == snapshot.id_to_pos.end()) {
            continue;
        }
        ranked.push_back(make_ranked_hit(snapshot, pos->second, normalized_query, query_tokens));
    }

    return top_hits(std::move(ranked), limit);
}

std::vector<SearchHit> build_substring_hits(const IndexSnapshot& snapshot,
                                            std::string_view normalized_query,
                                            std::size_t limit)
{
    std::vector<RankedHit> ranked;
    for (std::size_t pos = 0; pos < snapshot.normalized_names.size() && ranked.size() < kMaxLimit; ++pos) {
        if (snapshot.normalized_names[pos].find(normalized_query) != std::string::npos) {
            ranked.push_back(make_ranked_hit(snapshot, pos, normalized_query, {}, 30));
        }
    }

    return top_hits(std::move(ranked), limit);
}

} // namespace

SearchEngine::SearchEngine(std::size_t cache_capacity)
    : cache_(cache_capacity)
{
}

Result<SearchResponse> SearchEngine::search(const IndexSnapshot& snapshot, const SearchRequest& request)
{
    const auto started = std::chrono::steady_clock::now();
    if (request.query_utf8.size() > kMaxQueryBytes) {
        return Result<SearchResponse>::failure({ErrorCode::InvalidArgument, "search query is too long"});
    }

    const auto limit = clamp_limit(request.limit);
    const auto trimmed_query = trim_ascii(request.query_utf8);
    const auto normalized_query = normalize_index_text(trimmed_query);
    SearchResponse response;
    response.generation = snapshot.generation;

    if (normalized_query.empty()) {
        response.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        return Result<SearchResponse>::success(std::move(response));
    }

    const auto cache_key = make_cache_key(normalized_query, limit);
    if (auto cached = cache_.get(cache_key)) {
        response.generation = snapshot.generation;
        response.hits = cached->generation == snapshot.generation ? cached->hits : std::vector<SearchHit>{};
        response.from_cache = cached->generation == snapshot.generation;
        if (!response.from_cache) {
            cache_.clear();
        } else {
            response.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started);
            return Result<SearchResponse>::success(std::move(response));
        }
    }

    const auto query_tokens = tokenize(normalized_query);
    if (query_tokens.empty()) {
        response.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started);
        return Result<SearchResponse>::success(std::move(response));
    }

    std::vector<std::vector<FileId>> per_token_candidates;
    per_token_candidates.reserve(query_tokens.size());
    for (const auto& token : query_tokens) {
        per_token_candidates.push_back(candidates_for_token(snapshot, token));
    }

    std::sort(per_token_candidates.begin(), per_token_candidates.end(), [](const auto& left, const auto& right) {
        return left.size() < right.size();
    });

    std::vector<FileId> candidates = per_token_candidates.empty() ? std::vector<FileId>{} : per_token_candidates[0];
    for (std::size_t i = 1; i < per_token_candidates.size() && !candidates.empty(); ++i) {
        candidates = intersect_sorted(candidates, per_token_candidates[i]);
    }

    if (!candidates.empty()) {
        response.hits = build_index_hits(snapshot, candidates, normalized_query, query_tokens, limit);
    } else {
        response.hits = build_substring_hits(snapshot, normalized_query, limit);
    }

    cache_.put(cache_key, CachedSearch{snapshot.generation, response.hits});
    response.elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started);
    return Result<SearchResponse>::success(std::move(response));
}

void SearchEngine::clear_cache()
{
    cache_.clear();
}

} // namespace coredesk::index
