#pragma once

#include <cstdint>
#include <string>

namespace bt {

// Local calendar day of a unix time, e.g. "2026-10-27". Sorts chronologically.
std::string dayKey(int64_t unixSeconds);

// Short local date, e.g. "Oct 27".
std::string shortDate(int64_t unixSeconds);

// True when `day` is the calendar day right before `today` (both from dayKey).
bool isDayBefore(std::string const& day, std::string const& today);

}
