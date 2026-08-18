#pragma once

#include <atomic>
#include <memory>

namespace coredesk {

class CancellationToken {
public:
    CancellationToken();

    bool is_cancelled() const noexcept;

private:
    explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag);

    std::shared_ptr<std::atomic_bool> flag_;

    friend class CancellationSource;
};

class CancellationSource {
public:
    CancellationSource();

    CancellationToken token() const;
    void cancel() noexcept;

private:
    std::shared_ptr<std::atomic_bool> flag_;
};

} // namespace coredesk
