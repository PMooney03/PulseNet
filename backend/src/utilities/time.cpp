#include "pulsenet/utilities/time.hpp"

#include <iomanip>
#include <sstream>

namespace pulsenet {
namespace {

struct CivilDate {
    int year;
    unsigned month;
    unsigned day;
};

// Howard Hinnant's public-domain days-to-civil conversion.
// Used so UTC log timestamps do not depend on gmtime_r versus the reversed
// Microsoft gmtime_s arguments.
CivilDate civil_from_days(std::int64_t days) noexcept {
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned day_of_era = static_cast<unsigned>(days - era * 146097);
    const unsigned year_of_era =
        (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t year = static_cast<std::int64_t>(year_of_era) + era * 400;
    const unsigned day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const unsigned month_part = (5 * day_of_year + 2) / 153;
    const unsigned day = day_of_year - (153 * month_part + 2) / 5 + 1;
    const unsigned month = month_part < 10 ? month_part + 3 : month_part - 9;
    return CivilDate{static_cast<int>(year + (month <= 2 ? 1 : 0)), month, day};
}

}  // namespace

std::int64_t to_unix_ms(std::chrono::system_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
}

std::chrono::system_clock::time_point from_unix_ms(std::int64_t milliseconds) {
    return std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}};
}

std::string format_utc(std::chrono::system_clock::time_point time) {
    const auto milliseconds = to_unix_ms(time);
    std::int64_t seconds = milliseconds / 1000;
    int millis = static_cast<int>(milliseconds % 1000);
    if (millis < 0) {
        millis += 1000;
        --seconds;
    }

    std::int64_t days = seconds / 86400;
    std::int64_t time_of_day = seconds % 86400;
    if (time_of_day < 0) {
        time_of_day += 86400;
        --days;
    }

    const CivilDate date = civil_from_days(days);
    const auto hour = static_cast<int>(time_of_day / 3600);
    const auto minute = static_cast<int>((time_of_day % 3600) / 60);
    const auto second = static_cast<int>(time_of_day % 60);

    std::ostringstream out;
    out << std::setfill('0') << std::setw(4) << date.year << '-' << std::setw(2) << date.month << '-'
        << std::setw(2) << date.day << 'T' << std::setw(2) << hour << ':' << std::setw(2) << minute << ':'
        << std::setw(2) << second << '.' << std::setw(3) << millis << 'Z';
    return out.str();
}

}  // namespace pulsenet
