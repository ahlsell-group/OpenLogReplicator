/* Test for time zone offsets and TIMESTAMP WITH TIME ZONE values
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
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../builder/Builder.h"
#include "../common/Ctx.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/table/SysCol.h"
#include "../common/types/Data.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// Data::parseTimezone turns "+HH:MM" and zone aliases into seconds east of UTC (db-timezone, log-timezone, DBTIMEZONE),
// and Builder::processValue decodes TIMESTAMP WITH TIME ZONE: 7 date bytes, 4 optional fraction bytes, 2 time zone bytes.
using namespace OpenLogReplicator;

namespace {
    int failures = 0;

    void check(const std::string& name, bool ok, const std::string& detail = "") {
        if (!ok) {
            std::cerr << "FAIL " << name << (detail.empty() ? "" : ": " + detail) << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    void checkOffset(const std::string& value, int64_t expected) {
        int64_t out = 0;
        const bool parsed = Data::parseTimezone(value, out);
        check("parse " + value, parsed && out == expected,
              parsed ? "got " + std::to_string(out) + ", expected " + std::to_string(expected) : "rejected");
    }

    void checkRejected(const std::string& value) {
        int64_t out = 0;
        check("reject " + value, !Data::parseTimezone(value, out), "accepted as " + std::to_string(out));
    }

    class TestThread final : public Thread {
    public:
        explicit TestThread(Ctx* newCtx): Thread(newCtx, "test") {}
        void run() override {}
        std::string getName() const override { return "test"; }
    };

    // Records the last value handed to the column callbacks
    class TestBuilder final : public Builder {
    public:
        bool tzSeen{false};
        time_t timestamp{0};
        uint64_t fraction{0};
        std::string tz;
        std::string string;

        TestBuilder(Ctx* newCtx, Locales* newLocales, Metadata* newMetadata, const Format& newFormat):
                Builder(newCtx, newLocales, newMetadata, newFormat, 0) {}

        void value(const DbTable* table, const std::vector<uint8_t>& bytes) {
            tzSeen = false;
            string.clear();
            // An exact-size heap copy, so that a read past the value is caught by ASAN
            const std::unique_ptr<uint8_t[]> data(new uint8_t[bytes.size()]);
            std::copy(bytes.begin(), bytes.end(), data.get());
            processValue(nullptr, nullptr, table, 0, data.get(), static_cast<uint32_t>(bytes.size()), FileOffset(0), true, false);
        }

    protected:
        void columnFloat(const std::string&, double) override {}
        void columnDouble(const std::string&, long double) override {}
        void columnString(const std::string&) override { string.assign(valueBuffer, valueSize); }
        void columnNumber(const std::string&, int, int) override {}
        void columnRaw(const std::string&, const uint8_t*, uint64_t) override {}
        void columnRowId(const std::string&, RowId) override {}
        void columnTimestamp(const std::string&, time_t, uint64_t) override {}
        void columnTimestampTz(const std::string&, time_t newTimestamp, uint64_t newFraction, const std::string_view& newTz) override {
            tzSeen = true;
            timestamp = newTimestamp;
            fraction = newFraction;
            tz = newTz;
        }
        void processInsert(Seq, Scn, Time, LobCtx*, const XmlCtx*, const DbTable*, typeObj, typeDataObj, typeDba, typeSlot, FileOffset) override {}
        void processUpdate(Seq, Scn, Time, LobCtx*, const XmlCtx*, const DbTable*, typeObj, typeDataObj, typeDba, typeSlot, FileOffset) override {}
        void processDelete(Seq, Scn, Time, LobCtx*, const XmlCtx*, const DbTable*, typeObj, typeDataObj, typeDba, typeSlot, FileOffset) override {}
        void processDdl(Seq, Scn, Time, const DbTable*, typeObj) override {}
        void processBeginMessage(Seq, Time) override {}

    public:
        void processCommit() override {}
        void processCheckpoint(Seq, Scn, Time, FileOffset, bool) override {}
    };

    // 2026-10-26 12:34:56 UTC, as Oracle stores the date part of a TIMESTAMP WITH TIME ZONE
    constexpr time_t EPOCH = 1793018096;
    std::vector<uint8_t> datePart() {
        return {120, 126, 10, 26, 13, 35, 57};
    }

    std::vector<uint8_t> withFraction(uint32_t fraction, uint8_t tz1, uint8_t tz2) {
        std::vector<uint8_t> out = datePart();
        out.push_back(static_cast<uint8_t>(fraction >> 24));
        out.push_back(static_cast<uint8_t>(fraction >> 16));
        out.push_back(static_cast<uint8_t>(fraction >> 8));
        out.push_back(static_cast<uint8_t>(fraction));
        out.push_back(tz1);
        out.push_back(tz2);
        return out;
    }

    std::vector<uint8_t> withoutFraction(uint8_t tz1, uint8_t tz2) {
        std::vector<uint8_t> out = datePart();
        out.push_back(tz1);
        out.push_back(tz2);
        return out;
    }

    void checkValue(TestBuilder& builder, const DbTable* table, const std::string& name, const std::vector<uint8_t>& bytes,
                    uint64_t fraction, const std::string& tz) {
        builder.value(table, bytes);
        check(name + " decoded", builder.tzSeen, "sent as \"" + builder.string + "\"");
        if (!builder.tzSeen)
            return;
        check(name + " timestamp", builder.timestamp == EPOCH, "got " + std::to_string(builder.timestamp));
        check(name + " fraction", builder.fraction == fraction, "got " + std::to_string(builder.fraction));
        check(name + " time zone", builder.tz == tz, "got " + builder.tz + ", expected " + tz);
    }

    // Warnings the builder logs while decoding the value
    std::string warningsOf(TestBuilder& builder, const DbTable* table, const std::vector<uint8_t>& bytes) {
        std::ostringstream captured;
        std::streambuf* old = std::cerr.rdbuf(captured.rdbuf());
        builder.value(table, bytes);
        std::cerr.rdbuf(old);
        return captured.str();
    }

    size_t count(const std::string& text, const std::string& part) {
        size_t n = 0;
        for (size_t pos = text.find(part); pos != std::string::npos; pos = text.find(part, pos + 1))
            ++n;
        return n;
    }
}

int main() {
    // Two-digit hours and minutes, seconds east of UTC
    checkOffset("+00:00", 0);
    checkOffset("+01:00", 3600);
    checkOffset("+02:00", 7200);
    checkOffset("-05:00", -18000);
    checkOffset("+05:30", 19800);
    checkOffset("+05:45", 20700);
    checkOffset("+10:00", 36000);
    checkOffset("+12:45", 45900);
    checkOffset("+14:00", 50400);
    checkOffset("-03:45", -13500);
    checkOffset("-09:30", -34200);
    checkOffset("-12:00", -43200);
    // One-digit hours
    checkOffset("+1:00", 3600);
    checkOffset("+5:30", 19800);
    checkOffset("-9:00", -32400);
    checkOffset("-3:45", -13500);
    // Etc/GMT-N is N hours east of UTC (POSIX sign)
    checkOffset("Etc/GMT-14", 50400);
    checkOffset("Etc/GMT-10", 36000);
    checkOffset("Etc/GMT-1", 3600);
    checkOffset("Etc/GMT+1", -3600);
    checkOffset("Etc/GMT+5", -18000);
    checkOffset("Etc/GMT+12", -43200);
    // Other aliases
    checkOffset("UTC", 0);
    checkOffset("CET", 3600);
    checkOffset("HST", -36000);
    checkOffset("PST", -28800);
    checkOffset("PRC", 28800);
    // Invalid values
    checkRejected("+05:60");
    checkRejected("+5:99");
    checkRejected("05:00");
    checkRejected("+0500");
    checkRejected("");
    // Round trip through Data::timezoneToString
    for (const std::string value: {"+05:30", "-03:45", "+10:00", "+14:00", "-12:00", "+00:00"}) {
        int64_t out = 0;
        const bool parsed = Data::parseTimezone(value, out);
        check("round trip " + value, parsed && Data::timezoneToString(out) == value,
              parsed ? "printed as " + Data::timezoneToString(out) : "rejected");
    }

    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    const Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                        Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                        Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                        Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                        Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                        Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);
    TestBuilder builder(&ctx, &locales, &metadata, format);
    builder.initialize();

    const std::unique_ptr<DbTable> table(new DbTable(1001, 1001, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "TSTZ"));
    table->addColumn(new DbColumn(1, -1, 1, "C1", SysCol::COLTYPE::TIMESTAMP_WITH_TZ, 13, -1, 6, 0, 0, true, false, false, false,
                                  false, false, false, false, false));

    // Offset time zones: hour + 20, minute + 60
    checkValue(builder, table.get(), "13 bytes +01:00", withFraction(123456789, 21, 60), 123456789, "+01:00");
    checkValue(builder, table.get(), "13 bytes -03:30", withFraction(500, 17, 30), 500, "-03:30");
    checkValue(builder, table.get(), "13 bytes +05:45", withFraction(0, 25, 105), 0, "+05:45");
    // Region time zones
    checkValue(builder, table.get(), "13 bytes Europe/Stockholm", withFraction(1, 0x86, 0x54), 1, "Europe/Stockholm");

    // A region id missing from the map is sent as TZ? and logged once per id
    const std::string first = warningsOf(builder, table.get(), withFraction(0, 0xFF, 0xFF));
    check("unknown region sent as TZ?", builder.tzSeen && builder.tz == "TZ?", "got " + builder.tz);
    check("unknown region warning", count(first, "60044") == 1 && first.find("65535") != std::string::npos, "logged: " + first);
    const std::string second = warningsOf(builder, table.get(), withFraction(0, 0xFF, 0xFF));
    check("unknown region warning only once", count(second, "60044") == 0, "logged: " + second);
    const std::string other = warningsOf(builder, table.get(), withFraction(0, 0xFF, 0xFE));
    check("other unknown region warning", count(other, "60044") == 1 && other.find("65534") != std::string::npos, "logged: " + other);

    // Without fraction the time zone bytes follow the date
    checkValue(builder, table.get(), "9 bytes -03:30", withoutFraction(17, 30), 0, "-03:30");
    checkValue(builder, table.get(), "9 bytes +10:00", withoutFraction(30, 60), 0, "+10:00");
    checkValue(builder, table.get(), "9 bytes Europe/Stockholm", withoutFraction(0x86, 0x54), 0, "Europe/Stockholm");

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
