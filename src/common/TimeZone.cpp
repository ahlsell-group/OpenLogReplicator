/* Time zone of the database host
   Copyright (C) 2018-2026 Adam Leszczynski (aleszczynski@bersler.com)

This file is part of OpenLogReplicator.

This program is free software: you can redistribute it and/or
modify it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with this program; see the file LICENSE;
If not, see <http://www.gnu.org/licenses/>. */

#include <algorithm>
#include <array>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

#include "TimeZone.h"
#include "types/Data.h"

namespace {
    constexpr int64_t SECONDS_PER_DAY = 24 * 60 * 60;

    uint32_t read32(const std::vector<uint8_t>& buf, size_t pos) {
        return (static_cast<uint32_t>(buf[pos]) << 24) | (static_cast<uint32_t>(buf[pos + 1]) << 16) |
                (static_cast<uint32_t>(buf[pos + 2]) << 8) | static_cast<uint32_t>(buf[pos + 3]);
    }

    int64_t read64(const std::vector<uint8_t>& buf, size_t pos) {
        return static_cast<int64_t>((static_cast<uint64_t>(read32(buf, pos)) << 32) | static_cast<uint64_t>(read32(buf, pos + 4)));
    }

    bool parseNumber(const std::string& str, size_t& pos, int64_t& out) {
        const size_t start = pos;
        out = 0;
        while (pos < str.length() && str[pos] >= '0' && str[pos] <= '9' && pos - start < 4) {
            out = (out * 10) + (str[pos] - '0');
            ++pos;
        }
        return pos > start;
    }

    int64_t floorDiv(int64_t a, int64_t b) {
        return (a >= 0) ? a / b : -((-a + b - 1) / b);
    }
}

namespace OpenLogReplicator {
    TimeZone::TimeZone():
            name("+00:00") {
    }

    TimeZone::TimeZone(int64_t newFixedOffset):
            name(Data::timezoneToString(newFixedOffset)),
            fixedOffset(newFixedOffset) {
    }

    bool TimeZone::parse(const std::string& str, std::string& error) {
        int64_t offset;
        if (Data::parseTimezone(str, offset)) {
            *this = TimeZone(offset);
            return true;
        }

        if (!isValidName(str)) {
            error = "not a valid time zone name";
            return false;
        }

        std::string dir("/usr/share/zoneinfo");
        const char* tzDir = std::getenv("TZDIR");
        if (tzDir != nullptr && tzDir[0] != 0)
            dir = tzDir;

        TimeZone zone;
        if (!zone.loadTzif(dir + "/" + str, error))
            return false;
        zone.name = str;
        zone.fixed = false;
        *this = zone;
        return true;
    }

    time_t TimeZone::toUtc(int64_t local) const {
        if (fixed)
            return local - fixedOffset;

        // Walk the offset periods which can contain the instant, there is more than one only around a transition
        int64_t utc = local - MAX_OFFSET;
        bool found = false;
        int64_t result = 0;
        bool havePrevious = false;
        int64_t previousOffset = 0;
        bool gapFound = false;
        int64_t gapResult = 0;

        while (true) {
            const Period period = periodAt(utc);
            const int64_t candidate = local - period.offset;

            if (candidate >= period.begin && candidate < period.end) {
                // Repeated wall-clock time: take the first occurrence
                if (!found || candidate < result) {
                    result = candidate;
                    found = true;
                }
            } else if (havePrevious && !gapFound && local - previousOffset >= period.begin && candidate < period.begin) {
                // Skipped wall-clock time: keep the offset from before the transition
                gapResult = local - previousOffset;
                gapFound = true;
            }

            havePrevious = true;
            previousOffset = period.offset;
            if (period.end == INT64_MAX || period.end > local + MAX_OFFSET)
                break;
            utc = period.end;
        }

        if (found)
            return result;
        if (gapFound)
            return gapResult;
        return local - previousOffset;
    }

    bool TimeZone::isValidName(const std::string& str) {
        if (str.empty() || str.length() > 255 || str[0] == '/' || str.find("..") != std::string::npos)
            return false;

        for (const char c: str)
            if (!isalnum(static_cast<unsigned char>(c)) && c != '/' && c != '_' && c != '-' && c != '+')
                return false;
        return true;
    }

    bool TimeZone::isLeapYear(int64_t year) {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    // Days since 1970-01-01 for a proleptic Gregorian date
    int64_t TimeZone::daysFromCivil(int64_t year, int64_t month, int64_t day) {
        if (month <= 2)
            --year;
        const int64_t era = floorDiv(year, 400);
        const int64_t yoe = year - (era * 400);
        const int64_t doy = ((153 * (month + (month > 2 ? -3 : 9))) + 2) / 5 + day - 1;
        const int64_t doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
        return (era * 146097) + doe - 719468;
    }

    int64_t TimeZone::yearFromDays(int64_t days) {
        days += 719468;
        const int64_t era = floorDiv(days, 146097);
        const int64_t doe = days - (era * 146097);
        const int64_t yoe = (doe - (doe / 1460) + (doe / 36524) - (doe / 146096)) / 365;
        const int64_t doy = doe - ((365 * yoe) + (yoe / 4) - (yoe / 100));
        const int64_t mp = ((5 * doy) + 2) / 153;
        const int64_t month = mp + (mp < 10 ? 3 : -9);
        return yoe + (era * 400) + (month <= 2 ? 1 : 0);
    }

    bool TimeZone::loadTzif(const std::string& path, std::string& error) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            error = "can't open time zone file: " + path;
            return false;
        }
        const std::vector<uint8_t> buf((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // Header: magic, version, 15 unused bytes, then isutcnt, isstdcnt, leapcnt, timecnt, typecnt, charcnt
        constexpr size_t HEADER_SIZE = 44;
        if (buf.size() < HEADER_SIZE || memcmp(buf.data(), "TZif", 4) != 0) {
            error = "not a TZif time zone file: " + path;
            return false;
        }
        const uint8_t version = buf[4];
        size_t counts[6];
        for (size_t i = 0; i < 6; ++i)
            counts[i] = read32(buf, 20 + (i * 4));

        size_t timeSize = 4;
        size_t pos = HEADER_SIZE;
        if (version >= '2') {
            // Skip the 32-bit data block and use the 64-bit one
            pos += (counts[3] * 5) + (counts[4] * 6) + counts[5] + (counts[2] * 8) + counts[1] + counts[0];
            if (buf.size() < pos + HEADER_SIZE || memcmp(buf.data() + pos, "TZif", 4) != 0) {
                error = "truncated time zone file: " + path;
                return false;
            }
            for (size_t i = 0; i < 6; ++i)
                counts[i] = read32(buf, pos + 20 + (i * 4));
            pos += HEADER_SIZE;
            timeSize = 8;
        }

        const size_t timeCnt = counts[3];
        const size_t typeCnt = counts[4];
        const size_t blockSize = (timeCnt * (timeSize + 1)) + (typeCnt * 6) + counts[5] + (counts[2] * (timeSize + 4)) + counts[1] + counts[0];
        if (typeCnt == 0 || typeCnt > 255 || buf.size() < pos + blockSize) {
            error = "truncated time zone file: " + path;
            return false;
        }

        transitions.reserve(timeCnt);
        for (size_t i = 0; i < timeCnt; ++i) {
            transitions.push_back(timeSize == 8 ? read64(buf, pos) : static_cast<int32_t>(read32(buf, pos)));
            pos += timeSize;
        }
        transitionTypes.reserve(timeCnt);
        for (size_t i = 0; i < timeCnt; ++i) {
            if (buf[pos] >= typeCnt) {
                error = "corrupted time zone file: " + path;
                return false;
            }
            transitionTypes.push_back(buf[pos++]);
        }
        typeOffsets.reserve(typeCnt);
        for (size_t i = 0; i < typeCnt; ++i) {
            typeOffsets.push_back(static_cast<int32_t>(read32(buf, pos)));
            pos += 6;
        }
        pos += blockSize - (timeCnt * (timeSize + 1)) - (typeCnt * 6);

        // Footer with the POSIX TZ string for instants after the last transition
        if (version >= '2' && pos < buf.size() && buf[pos] == '\n') {
            const auto end = std::find(buf.begin() + static_cast<std::ptrdiff_t>(pos) + 1, buf.end(), '\n');
            if (end != buf.end() && end > buf.begin() + static_cast<std::ptrdiff_t>(pos) + 1) {
                const std::string posixTz(buf.begin() + static_cast<std::ptrdiff_t>(pos) + 1, end);
                if (!parsePosixTz(posixTz)) {
                    error = "unsupported rule: " + posixTz + " in time zone file: " + path;
                    return false;
                }
                hasRule = true;
            }
        }

        return true;
    }

    bool TimeZone::skipName(const std::string& str, size_t& pos) {
        if (pos < str.length() && str[pos] == '<') {
            const size_t end = str.find('>', pos);
            if (end == std::string::npos)
                return false;
            pos = end + 1;
            return true;
        }

        const size_t start = pos;
        while (pos < str.length() && isalpha(static_cast<unsigned char>(str[pos])))
            ++pos;
        return pos - start >= 3;
    }

    // [+-]hh[:mm[:ss]]
    bool TimeZone::parseOffset(const std::string& str, size_t& pos, int64_t& out) {
        int64_t sign = 1;
        if (pos < str.length() && (str[pos] == '+' || str[pos] == '-')) {
            if (str[pos] == '-')
                sign = -1;
            ++pos;
        }

        int64_t hours;
        int64_t minutes = 0;
        int64_t seconds = 0;
        if (!parseNumber(str, pos, hours))
            return false;
        if (pos < str.length() && str[pos] == ':') {
            ++pos;
            if (!parseNumber(str, pos, minutes))
                return false;
            if (pos < str.length() && str[pos] == ':') {
                ++pos;
                if (!parseNumber(str, pos, seconds))
                    return false;
            }
        }
        out = sign * ((hours * 3600) + (minutes * 60) + seconds);
        return true;
    }

    // Jn, n or Mm.w.d, optionally followed by /time
    bool TimeZone::parseRule(const std::string& str, size_t& pos, Rule& rule) {
        if (pos >= str.length())
            return false;

        if (str[pos] == 'J') {
            ++pos;
            rule.type = Rule::TYPE::JULIAN_NO_LEAP;
            if (!parseNumber(str, pos, rule.day) || rule.day < 1 || rule.day > 365)
                return false;
        } else if (str[pos] == 'M') {
            ++pos;
            rule.type = Rule::TYPE::MONTH_WEEK_DAY;
            if (!parseNumber(str, pos, rule.month) || rule.month < 1 || rule.month > 12 || pos >= str.length() || str[pos] != '.')
                return false;
            ++pos;
            if (!parseNumber(str, pos, rule.week) || rule.week < 1 || rule.week > 5 || pos >= str.length() || str[pos] != '.')
                return false;
            ++pos;
            if (!parseNumber(str, pos, rule.day) || rule.day > 6)
                return false;
        } else {
            rule.type = Rule::TYPE::JULIAN;
            if (!parseNumber(str, pos, rule.day) || rule.day > 365)
                return false;
        }

        if (pos < str.length() && str[pos] == '/') {
            ++pos;
            if (!parseOffset(str, pos, rule.time))
                return false;
        }
        return true;
    }

    // std offset [dst [offset] [,start[/time],end[/time]]], offsets are positive west of UTC
    bool TimeZone::parsePosixTz(const std::string& str) {
        size_t pos = 0;
        int64_t offset;
        if (!skipName(str, pos) || !parseOffset(str, pos, offset))
            return false;
        ruleStdOffset = -offset;
        ruleHasDst = false;
        if (pos >= str.length())
            return true;

        if (!skipName(str, pos))
            return false;
        ruleHasDst = true;
        ruleDstOffset = ruleStdOffset + 3600;
        if (pos < str.length() && str[pos] != ',') {
            if (!parseOffset(str, pos, offset))
                return false;
            ruleDstOffset = -offset;
        }

        if (pos >= str.length()) {
            // No rule given, POSIX falls back to the US rules
            ruleStart.type = Rule::TYPE::MONTH_WEEK_DAY;
            ruleStart.month = 3;
            ruleStart.week = 2;
            ruleStart.day = 0;
            ruleEnd.type = Rule::TYPE::MONTH_WEEK_DAY;
            ruleEnd.month = 11;
            ruleEnd.week = 1;
            ruleEnd.day = 0;
            return true;
        }

        if (str[pos] != ',')
            return false;
        ++pos;
        if (!parseRule(str, pos, ruleStart) || pos >= str.length() || str[pos] != ',')
            return false;
        ++pos;
        if (!parseRule(str, pos, ruleEnd))
            return false;
        return pos == str.length();
    }

    // UTC instant of a rule transition in the given year, the rule time is wall-clock time before the transition
    int64_t TimeZone::ruleTransition(int64_t year, const Rule& rule, int64_t offsetBefore) {
        const int64_t yearStart = daysFromCivil(year, 1, 1);
        int64_t dayOfYear = 0;

        switch (rule.type) {
            case Rule::TYPE::JULIAN_NO_LEAP:
                dayOfYear = rule.day - 1;
                if (isLeapYear(year) && rule.day >= 60)
                    ++dayOfYear;
                break;

            case Rule::TYPE::JULIAN:
                dayOfYear = rule.day;
                break;

            case Rule::TYPE::MONTH_WEEK_DAY: {
                const int64_t monthStart = daysFromCivil(year, rule.month, 1);
                const int64_t nextMonthStart = (rule.month == 12) ? daysFromCivil(year + 1, 1, 1) : daysFromCivil(year, rule.month + 1, 1);
                // 1970-01-01 was a Thursday
                const int64_t weekDay = ((monthStart % 7) + 11) % 7;
                int64_t day = 1 + ((rule.day - weekDay + 7) % 7) + ((rule.week - 1) * 7);
                while (day > nextMonthStart - monthStart)
                    day -= 7;
                dayOfYear = monthStart + day - 1 - yearStart;
                break;
            }

            case Rule::TYPE::NONE:
                break;
        }

        return ((yearStart + dayOfYear) * SECONDS_PER_DAY) + rule.time - offsetBefore;
    }

    TimeZone::Period TimeZone::rulePeriodAt(int64_t utc) const {
        if (!ruleHasDst)
            return {ruleStdOffset, INT64_MIN, INT64_MAX};

        // Transitions of the surrounding years are enough to bound the period
        const int64_t year = yearFromDays(floorDiv(utc + ruleStdOffset, SECONDS_PER_DAY));
        std::array<std::pair<int64_t, bool>, 6> ruleTransitions;
        for (int64_t i = 0; i < 3; ++i) {
            ruleTransitions[i * 2] = {ruleTransition(year - 1 + i, ruleStart, ruleStdOffset), true};
            ruleTransitions[(i * 2) + 1] = {ruleTransition(year - 1 + i, ruleEnd, ruleDstOffset), false};
        }
        std::sort(ruleTransitions.begin(), ruleTransitions.end());

        Period period{ruleStdOffset, INT64_MIN, INT64_MAX};
        for (const auto& [instant, dstStarts]: ruleTransitions) {
            if (instant <= utc) {
                period.begin = instant;
                period.offset = dstStarts ? ruleDstOffset : ruleStdOffset;
            } else {
                period.end = instant;
                break;
            }
        }
        return period;
    }

    TimeZone::Period TimeZone::periodAt(int64_t utc) const {
        if (transitions.empty()) {
            if (hasRule)
                return rulePeriodAt(utc);
            return {typeOffsets[0], INT64_MIN, INT64_MAX};
        }

        // RFC 8536: the first time type applies before the first transition
        if (utc < transitions.front())
            return {typeOffsets[0], INT64_MIN, transitions.front()};

        if (hasRule && utc >= transitions.back()) {
            Period period = rulePeriodAt(utc);
            if (period.begin < transitions.back())
                period.begin = transitions.back();
            return period;
        }

        const auto it = std::upper_bound(transitions.begin(), transitions.end(), utc);
        const size_t idx = static_cast<size_t>(it - transitions.begin()) - 1;
        return {typeOffsets[transitionTypes[idx]], transitions[idx], (idx + 1 < transitions.size()) ? transitions[idx + 1] : INT64_MAX};
    }
}
