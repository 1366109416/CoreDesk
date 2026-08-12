#include "coredesk/common/Error.h"

namespace coredesk {

std::string_view to_string(ErrorCode code) noexcept
{
    switch (code) {
    case ErrorCode::Ok:
        return "Ok";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::PathNotFound:
        return "PathNotFound";
    case ErrorCode::PermissionDenied:
        return "PermissionDenied";
    case ErrorCode::Busy:
        return "Busy";
    case ErrorCode::Cancelled:
        return "Cancelled";
    case ErrorCode::IndexNotReady:
        return "IndexNotReady";
    case ErrorCode::ProtocolError:
        return "ProtocolError";
    case ErrorCode::PayloadTooLarge:
        return "PayloadTooLarge";
    case ErrorCode::ConnectionFailed:
        return "ConnectionFailed";
    case ErrorCode::Timeout:
        return "Timeout";
    case ErrorCode::TargetExists:
        return "TargetExists";
    case ErrorCode::IoError:
        return "IoError";
    case ErrorCode::HashMismatch:
        return "HashMismatch";
    case ErrorCode::InternalError:
        return "InternalError";
    }

    return "Unknown";
}

} // namespace coredesk
