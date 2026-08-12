#pragma once

#include "coredesk/common/Error.h"

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace coredesk {

template <class T>
class Result {
public:
    Result(const T& value) : storage_(value) {}
    Result(T&& value) : storage_(std::move(value)) {}
    Result(Error error) : storage_(std::move(error)) {}

    static Result<T> success(T value)
    {
        return Result<T>(std::move(value));
    }

    static Result<T> failure(Error error)
    {
        return Result<T>(std::move(error));
    }

    bool ok() const noexcept
    {
        return std::holds_alternative<T>(storage_);
    }

    T& value() &
    {
        if (!ok()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::get<T>(storage_);
    }

    const T& value() const&
    {
        if (!ok()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::get<T>(storage_);
    }

    T&& value() &&
    {
        if (!ok()) {
            throw std::logic_error("Result does not contain a value");
        }
        return std::move(std::get<T>(storage_));
    }

    Error& error() &
    {
        if (ok()) {
            throw std::logic_error("Result does not contain an error");
        }
        return std::get<Error>(storage_);
    }

    const Error& error() const&
    {
        if (ok()) {
            throw std::logic_error("Result does not contain an error");
        }
        return std::get<Error>(storage_);
    }

private:
    static_assert(!std::is_void_v<T>, "Use Result<void> for void results");

    std::variant<T, Error> storage_;
};

template <>
class Result<void> {
public:
    Result() = default;
    Result(Error error) : error_(std::move(error)) {}

    static Result<void> success()
    {
        return Result<void>();
    }

    static Result<void> failure(Error error)
    {
        return Result<void>(std::move(error));
    }

    bool ok() const noexcept
    {
        return error_.code == ErrorCode::Ok;
    }

    void value() const
    {
        if (!ok()) {
            throw std::logic_error("Result does not contain a value");
        }
    }

    Error& error() &
    {
        if (ok()) {
            throw std::logic_error("Result does not contain an error");
        }
        return error_;
    }

    const Error& error() const&
    {
        if (ok()) {
            throw std::logic_error("Result does not contain an error");
        }
        return error_;
    }

private:
    Error error_{ErrorCode::Ok, {}};
};

} // namespace coredesk
