#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace pulsenet {

enum class ErrorCode { Validation, NotFound, Constraint, Database };

std::string_view to_string(ErrorCode code);

struct Error {
    ErrorCode code{ErrorCode::Database};
    std::string message;
};

// Expected failures (validation, missing rows, constraint violations) are
// values. Exceptions are reserved for broken infrastructure, such as a
// database file that cannot be opened.
template <typename T>
class Result {
public:
    static Result success(T value) {
        Result result;
        result.value_ = std::move(value);
        return result;
    }

    static Result failure(Error error) {
        Result result;
        result.error_ = std::move(error);
        return result;
    }

    [[nodiscard]] bool ok() const { return value_.has_value(); }
    [[nodiscard]] explicit operator bool() const { return ok(); }

    [[nodiscard]] T& value() & {
        ensure();
        return *value_;
    }

    [[nodiscard]] const T& value() const& {
        ensure();
        return *value_;
    }

    [[nodiscard]] const Error& error() const { return error_; }

private:
    void ensure() const {
        if (!value_) {
            throw std::logic_error("Result has no value: " + error_.message);
        }
    }

    std::optional<T> value_;
    Error error_{};
};

class Status {
public:
    static Status success() { return Status{}; }

    static Status failure(Error error) {
        Status status;
        status.error_ = std::move(error);
        return status;
    }

    [[nodiscard]] bool ok() const { return !error_.has_value(); }
    [[nodiscard]] explicit operator bool() const { return ok(); }

    [[nodiscard]] const Error& error() const {
        if (!error_) {
            throw std::logic_error("Status has no error");
        }
        return *error_;
    }

private:
    std::optional<Error> error_;
};

}  // namespace pulsenet
