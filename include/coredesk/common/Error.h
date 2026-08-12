#pragma once

#include <string>
#include <string_view>

namespace coredesk {

enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    PathNotFound,
    PermissionDenied,
    Busy,
    Cancelled,
    IndexNotReady,
    ProtocolError,
    PayloadTooLarge,
    ConnectionFailed,
    Timeout,
    TargetExists,
    IoError,
    HashMismatch,
    InternalError
};

struct Error {
    ErrorCode code{ErrorCode::Ok};
    std::string message;
};

std::string_view to_string(ErrorCode code) noexcept;

} // namespace coredesk
