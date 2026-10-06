/* Test for TimeZone class
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

#include <iostream>
#include <string>

#include "../common/TimeZone.h"
#include "../common/types/Time.h"

// Expected values computed independently with Python zoneinfo. "local" values are the wall-clock time expressed
// as seconds since 1970-01-01 00:00:00 as if it was UTC, which is what Time::toEpoch(0) returns.
namespace {
    int failures = 0;

    void check(const std::string& name, int64_t actual, int64_t expected) {
        if (actual != expected) {
            std::cerr << "FAIL " << name << ": got " << actual << ", expected " << expected << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    OpenLogReplicator::TimeZone zone(const std::string& name) {
        OpenLogReplicator::TimeZone tz;
        std::string error;
        if (!tz.parse(name, error)) {
            std::cerr << "FAIL can't load " << name << ": " << error << "\n";
            ++failures;
        }
        return tz;
    }

    // Redo log encoding of a wall-clock time, see Time::toEpoch
    OpenLogReplicator::Time redoTime(uint64_t year, uint64_t month, uint64_t day, uint64_t hour, uint64_t minute, uint64_t second) {
        return OpenLogReplicator::Time(static_cast<uint32_t>((((((((year - 1988) * 12) + month - 1) * 31) + day - 1) * 24 + hour) * 60 + minute) * 60 + second));
    }
}

int main() {
    using OpenLogReplicator::TimeZone;

    // Fixed offsets
    TimeZone fixed;
    std::string error;
    check("fixed parse +02:00", fixed.parse("+02:00", error) ? 1 : 0, 1);
    check("fixed isFixed", fixed.isFixed() ? 1 : 0, 1);
    check("fixed offset", fixed.getFixedOffset(), 7200);
    check("fixed toUtc", fixed.toUtc(1784116800), 1784116800 - 7200);
    check("fixed toString", fixed.toString() == "+02:00" ? 1 : 0, 1);
    check("fixed -05:00", fixed.parse("-05:00", error) && fixed.getFixedOffset() == -18000 ? 1 : 0, 1);
    check("fixed UTC alias", fixed.parse("UTC", error) && fixed.isFixed() && fixed.getFixedOffset() == 0 ? 1 : 0, 1);
    check("default ctor", TimeZone().toUtc(1000), 1000);
    check("offset ctor", TimeZone(3600).toUtc(1000), 1000 - 3600);
    check("offset ctor toString", TimeZone(3600).toString() == "+01:00" ? 1 : 0, 1);

    // Invalid values
    TimeZone bad;
    check("invalid name", bad.parse("Mars/Olympus_Mons", error) ? 1 : 0, 0);
    check("invalid path", bad.parse("../../etc/passwd", error) ? 1 : 0, 0);
    check("invalid absolute", bad.parse("/etc/passwd", error) ? 1 : 0, 0);
    check("invalid format", bad.parse("+2", error) ? 1 : 0, 0);
    check("invalid empty", bad.parse("", error) ? 1 : 0, 0);

    // Europe/Stockholm: CET (+01:00) in winter, CEST (+02:00) in summer, transitions last Sunday of March/October
    const TimeZone sto = zone("Europe/Stockholm");
    check("sto isFixed", sto.isFixed() ? 1 : 0, 0);
    check("sto toString", sto.toString() == "Europe/Stockholm" ? 1 : 0, 1);
    check("sto winter 2026-01-15 12:00:00", sto.toUtc(1768478400), 1768474800);
    check("sto summer 2026-07-15 12:00:00", sto.toUtc(1784116800), 1784109600);
    // DST start 2026-03-29: 02:00 CET -> 03:00 CEST
    check("sto before gap 2026-03-29 01:59:59", sto.toUtc(1774749599), 1774745999);
    check("sto in gap 2026-03-29 02:30:00 keeps +01:00", sto.toUtc(1774751400), 1774747800);
    check("sto after gap 2026-03-29 03:00:00", sto.toUtc(1774753200), 1774746000);
    // DST end 2026-10-25: 03:00 CEST -> 02:00 CET, 02:00-02:59:59 occurs twice
    check("sto before overlap 2026-10-25 01:59:59", sto.toUtc(1792893599), 1792886399);
    check("sto overlap 2026-10-25 02:30:00 first occurrence", sto.toUtc(1792895400), 1792888200);
    check("sto overlap 2026-10-25 02:59:59 first occurrence", sto.toUtc(1792897199), 1792889999);
    check("sto after overlap 2026-10-25 03:00:00", sto.toUtc(1792897200), 1792893600);
    // Beyond the transitions stored in the file, POSIX TZ rule from the footer
    check("sto rule 2040-07-01 12:00:00", sto.toUtc(2224756800), 2224749600);
    check("sto rule 2040-01-01 12:00:00", sto.toUtc(2209032000), 2209028400);
    // Historical
    check("sto 1995-07-01 12:00:00", sto.toUtc(804600000), 804592800);
    check("sto 1970-07-01 12:00:00 no DST", sto.toUtc(15681600), 15678000);

    // Time -> epoch through the zone
    check("Time 2026-07-15 12:00:00 via zone", redoTime(2026, 7, 15, 12, 0, 0).toEpoch(sto), 1784109600);
    check("Time 2026-01-15 12:00:00 via zone", redoTime(2026, 1, 15, 12, 0, 0).toEpoch(sto), 1768474800);
    check("Time 2026-07-15 12:00:00 via fixed", redoTime(2026, 7, 15, 12, 0, 0).toEpoch(TimeZone(7200)), 1784109600);
    check("Time toEpoch(0) is wall-clock", redoTime(2026, 7, 15, 12, 0, 0).toEpoch(0), 1784116800);

    // Southern hemisphere, DST spans the turn of the year
    const TimeZone syd = zone("Australia/Sydney");
    check("syd 2026-01-15 12:00:00 AEDT", syd.toUtc(1768478400), 1768438800);
    check("syd 2026-07-15 12:00:00 AEST", syd.toUtc(1784116800), 1784080800);
    check("syd rule 2041-01-15 12:00:00 AEDT", syd.toUtc(2241864000), 2241824400);
    check("syd rule 2041-07-15 12:00:00 AEST", syd.toUtc(2257502400), 2257466400);

    // No DST, half-hour offset
    const TimeZone kolkata = zone("Asia/Kolkata");
    check("kolkata 2026-05-01 12:00:00", kolkata.toUtc(1777636800), 1777617000);

    // Negative offset with DST
    const TimeZone ny = zone("America/New_York");
    check("ny gap 2026-03-08 02:30:00 keeps -05:00", ny.toUtc(1772937000), 1772955000);
    check("ny winter 2026-12-01 08:00:00", ny.toUtc(1796112000), 1796130000);

    const TimeZone utc = zone("Etc/UTC");
    check("Etc/UTC", utc.toUtc(1777636800), 1777636800);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
