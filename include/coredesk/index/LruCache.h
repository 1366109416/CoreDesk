#pragma once

#include <cstddef>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace coredesk::index {

template <class Key, class Value>
class LruCache {
public:
    explicit LruCache(std::size_t capacity = 128)
        : capacity_(capacity == 0 ? 1 : capacity)
    {
    }

    std::optional<Value> get(const Key& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(key);
        if (found == entries_.end()) {
            return std::nullopt;
        }

        items_.splice(items_.begin(), items_, found->second);
        return found->second->second;
    }

    void put(Key key, Value value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(key);
        if (found != entries_.end()) {
            found->second->second = std::move(value);
            items_.splice(items_.begin(), items_, found->second);
            return;
        }

        items_.emplace_front(std::move(key), std::move(value));
        entries_[items_.front().first] = items_.begin();

        if (items_.size() > capacity_) {
            auto last = std::prev(items_.end());
            entries_.erase(last->first);
            items_.pop_back();
        }
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        items_.clear();
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    std::size_t capacity() const noexcept
    {
        return capacity_;
    }

private:
    using Item = std::pair<Key, Value>;
    using List = std::list<Item>;

    std::size_t capacity_;
    mutable std::mutex mutex_;
    List items_;
    std::unordered_map<Key, typename List::iterator> entries_;
};

} // namespace coredesk::index
