#include "coredesk/index/IndexBuilder.h"

#include "coredesk/index/Tokenizer.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace coredesk::index {
namespace {

std::string normalized_record_name(const filesystem::FileRecord& record)
{
    auto text = path_to_index_text(record.file_name);
    text.push_back(' ');
    text += path_to_index_text(record.relative_path);
    return normalize_index_text(text);
}

void add_tokens_for_path(std::unordered_map<std::string, std::unordered_set<FileId>>& token_sets,
                         const std::filesystem::path& path,
                         FileId id)
{
    for (auto& token : tokenize_path(path)) {
        token_sets[std::move(token)].insert(id);
    }
}

} // namespace

Result<std::shared_ptr<const IndexSnapshot>> IndexBuilder::build(IndexGeneration generation,
                                                                 filesystem::ScanOutput scan,
                                                                 CancellationToken token)
{
    if (token.is_cancelled()) {
        return Result<std::shared_ptr<const IndexSnapshot>>::failure({ErrorCode::Cancelled, "index build cancelled"});
    }

    auto snapshot = std::make_shared<IndexSnapshot>();
    snapshot->generation = generation;
    snapshot->root = std::move(scan.root);
    snapshot->records = std::move(scan.records);
    snapshot->id_to_pos.reserve(snapshot->records.size());
    snapshot->normalized_names.reserve(snapshot->records.size());

    std::unordered_map<std::string, std::unordered_set<FileId>> token_sets;

    for (std::size_t pos = 0; pos < snapshot->records.size(); ++pos) {
        if (token.is_cancelled()) {
            return Result<std::shared_ptr<const IndexSnapshot>>::failure(
                {ErrorCode::Cancelled, "index build cancelled"});
        }

        auto& record = snapshot->records[pos];
        const FileId id = static_cast<FileId>(pos + 1);
        record.id = id;
        snapshot->id_to_pos[id] = pos;
        snapshot->normalized_names.push_back(normalized_record_name(record));

        add_tokens_for_path(token_sets, record.file_name, id);
        add_tokens_for_path(token_sets, record.extension, id);
        add_tokens_for_path(token_sets, record.relative_path, id);
    }

    snapshot->token_index.reserve(token_sets.size());
    snapshot->sorted_tokens.reserve(token_sets.size());
    for (auto& [token_text, ids] : token_sets) {
        if (token.is_cancelled()) {
            return Result<std::shared_ptr<const IndexSnapshot>>::failure(
                {ErrorCode::Cancelled, "index build cancelled"});
        }

        PostingList posting;
        posting.ids.assign(ids.begin(), ids.end());
        std::sort(posting.ids.begin(), posting.ids.end());
        snapshot->sorted_tokens.push_back(token_text);
        snapshot->token_index.emplace(std::move(token_text), std::move(posting));
    }

    std::sort(snapshot->sorted_tokens.begin(), snapshot->sorted_tokens.end());
    snapshot->sorted_tokens.erase(std::unique(snapshot->sorted_tokens.begin(), snapshot->sorted_tokens.end()),
                                  snapshot->sorted_tokens.end());

    return Result<std::shared_ptr<const IndexSnapshot>>::success(std::move(snapshot));
}

} // namespace coredesk::index
