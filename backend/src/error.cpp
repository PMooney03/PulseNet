#include "pulsenet/error.hpp"

#include <stdexcept>

namespace pulsenet {
namespace {

[[noreturn]] void unknown_error_code() { throw std::logic_error("unknown error code"); }

}  // namespace

std::string_view to_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::Validation:
            return "validation";
        case ErrorCode::NotFound:
            return "not_found";
        case ErrorCode::Constraint:
            return "constraint";
        case ErrorCode::Database:
            return "database";
    }
    unknown_error_code();
}

}  // namespace pulsenet