#pragma once

#include "coredesk/common/Cancellation.h"
#include "coredesk/common/Result.h"
#include "coredesk/common/Types.h"
#include "coredesk/filesystem/FileScanner.h"
#include "coredesk/index/IndexSnapshot.h"

#include <memory>

namespace coredesk::index {

class IndexBuilder {
public:
    Result<std::shared_ptr<const IndexSnapshot>> build(IndexGeneration generation,
                                                       filesystem::ScanOutput scan,
                                                       CancellationToken token);
};

} // namespace coredesk::index
