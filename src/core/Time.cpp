#include "Time.hpp"

#include <cstdio>
#include <ctime>

namespace bt {

namespace {
    bool localTm(int64_t t, std::tm& out) {
        std::time_t tt = static_cast<std::time_t>(t);
#ifdef _WIN32
        return localtime_s(&out, &tt) == 0;
#else
        return localtime_r(&tt, &out) != nullptr;
#endif
    }

    // days since 1970-01-01 for a civil date (Howard Hinnant's algorithm)
    long daysFromCivil(int y, unsigned m, unsigned d) {
        y -= m <= 2;
        const long era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<long>(doe) - 719468;
    }

    // parses "YYYY-MM-DD"
    bool parseDay(std::string const& s, long& days) {
        if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
        auto num = [&](size_t from, size_t len, int& out) {
            out = 0;
            for (size_t i = from; i < from + len; ++i) {
                if (s[i] < '0' || s[i] > '9') return false;
                out = out * 10 + (s[i] - '0');
            }
            return true;
        };
        int y = 0, m = 0, d = 0;
        if (!num(0, 4, y) || !num(5, 2, m) || !num(8, 2, d) || m < 1 || m > 12 || d < 1 || d > 31) return false;
        days = daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
        return true;
    }
}

std::string dayKey(int64_t t) {
    std::tm tm{};
    if (!localTm(t, tm)) return "1970-01-01";
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
    return buf;
}

std::string shortDate(int64_t t) {
    static char const* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::tm tm{};
    if (t <= 0 || !localTm(t, tm)) return "-";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s %d", months[tm.tm_mon % 12], tm.tm_mday);
    return buf;
}

bool isDayBefore(std::string const& day, std::string const& today) {
    long a = 0, b = 0;
    return parseDay(day, a) && parseDay(today, b) && b - a == 1;
}

}
