#include "coredesk/common/Cancellation.h"

namespace coredesk {

CancellationToken::CancellationToken()
    : flag_(std::make_shared<std::atomic_bool>(false))
{
}

CancellationToken::CancellationToken(std::shared_ptr<std::atomic_bool> flag)
    : flag_(std::move(flag))
{
}

bool CancellationToken::is_cancelled() const noexcept
{
    return flag_ && flag_->load(std::memory_order_relaxed);
}

CancellationSource::CancellationSource()
    : flag_(std::make_shared<std::atomic_bool>(false))
{
}

CancellationToken CancellationSource::token() const
{
    return CancellationToken(flag_);
}

void CancellationSource::cancel() noexcept
{
    flag_->store(true, std::memory_order_relaxed);
}

} // namespace coredesk
