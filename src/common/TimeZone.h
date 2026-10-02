/* Header for TimeZone class
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

#ifndef TIME_ZONE_H_
#define TIME_ZONE_H_

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

namespace OpenLogReplicator {
    // Time zone of the database host, used to convert wall-clock timestamps stored in redo logs to UTC epoch.
    // Either a fixed offset ("+HH:MM") or a zone from the system tz database ("Europe/Warsaw"), read from TZif
    // files in $TZDIR (default /usr/share/zoneinfo), including the POSIX TZ footer rule for future dates.
    class TimeZone final {
        // Transition rule from the POSIX TZ string, e.g. M3.5.0/2
        struct Rule final {
            enum class TYPE : unsigned char {
                NONE, JULIAN_NO_LEAP, JULIAN, MONTH_WEEK_DAY
            };

            TYPE type{TYPE::NONE};
            int64_t day{0};
            int64_t week{0};
            int64_t month{0};
            int64_t time{2 * 60 * 60};
        };

        // Range of UTC instants with one offset
        struct Period final {
            int64_t offset;
            int64_t begin;
            int64_t end;
        };

        static constexpr int64_t MAX_OFFSET{14 * 60 * 60};

        std::string name;
        bool fixed{true};
        int64_t fixedOffset{0};

        std::vector<int64_t> transitions;
        std::vector<uint8_t> transitionTypes;
        std::vector<int64_t> typeOffsets;

        bool hasRule{false};
        bool ruleHasDst{false};
        int64_t ruleStdOffset{0};
        int64_t ruleDstOffset{0};
        Rule ruleStart;
        Rule ruleEnd;

        static bool isValidName(const std::string& str);
        static int64_t daysFromCivil(int64_t year, int64_t month, int64_t day);
        static int64_t yearFromDays(int64_t days);
        static bool isLeapYear(int64_t year);
        static bool parseOffset(const std::string& str, size_t& pos, int64_t& out);
        static bool parseRule(const std::string& str, size_t& pos, Rule& rule);
        static bool skipName(const std::string& str, size_t& pos);
        static int64_t ruleTransition(int64_t year, const Rule& rule, int64_t offsetBefore);

        bool loadTzif(const std::string& path, std::string& error);
        bool parsePosixTz(const std::string& str);
        [[nodiscard]] Period rulePeriodAt(int64_t utc) const;
        [[nodiscard]] Period periodAt(int64_t utc) const;

    public:
        TimeZone();
        explicit TimeZone(int64_t newFixedOffset);

        // Accepts "+HH:MM"/"-HH:MM" (and the aliases handled by Data::parseTimezone) or a tz database zone name
        bool parse(const std::string& str, std::string& error);

        // Converts wall-clock time of the host (seconds since 1970-01-01 00:00:00 as if it was UTC) to UTC epoch.
        // Ambiguous wall-clock times (repeated hour when DST ends) resolve to the first occurrence, i.e. the DST
        // offset. Wall-clock times which do not exist (skipped hour when DST starts) use the offset in effect
        // before the transition.
        [[nodiscard]] time_t toUtc(int64_t local) const;

        [[nodiscard]] bool isFixed() const {
            return fixed;
        }

        [[nodiscard]] int64_t getFixedOffset() const {
            return fixedOffset;
        }

        [[nodiscard]] const std::string& toString() const {
            return name;
        }
    };
}

#endif
