#pragma once

#include <string>
#include <string_view>

namespace pulsenet {

inline std::string to_lower_ascii(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            out.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }
    return out;
}

inline std::string trim_ascii(std::string_view value) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto begin = value.find_first_not_of(whitespace);
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(whitespace);
    return std::string{value.substr(begin, end - begin + 1)};
}

}  // namespace pulsenet
