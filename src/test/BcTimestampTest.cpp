/* Test for DATE and TIMESTAMP values before Christ
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

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/table/SysCol.h"
#include "../common/types/Data.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// Date fields are mapped to epoch with the proleptic Gregorian calendar, the same as for AD dates before 1582. Year N BC
// is astronomical year 1 - N, so 1 BC is year 0 and a leap year. The fraction of a second adds to the epoch second for
// BC values as it does for AD values.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        // The JSON a column with this value gives: "C":<value>
        static std::string column(Builder& builder, const DbTable* table, const uint8_t* data, uint32_t size) {
            const uint64_t start = builder.lastBuilderSize + builder.messagePosition;
            builder.processValue(nullptr, nullptr, table, 0, data, size, FileOffset(0), true, false);
            std::string json(reinterpret_cast<const char*>(builder.lastBuilderQueue->data) + start,
                             builder.lastBuilderSize + builder.messagePosition - start);
            if (!json.empty() && json[0] == ',')
                json.erase(0, 1);
            return json;
        }
    };
}

namespace {
    int failures = 0;

    void check(const std::string& name, const std::string& actual, const std::string& expected) {
        if (actual != expected) {
            std::cerr << "FAIL " << name << ": got " << actual << ", expected " << expected << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    class TestThread final : public OpenLogReplicator::Thread {
    public:
        explicit TestThread(OpenLogReplicator::Ctx* newCtx): Thread(newCtx, "test") {}
        void run() override {}
        std::string getName() const override { return "test"; }
    };

    // Days from 1970-01-01 in the proleptic Gregorian calendar, astronomical year numbering (H. Hinnant, days_from_civil)
    int64_t daysFromCivil(int64_t y, int64_t m, int64_t d) {
        y -= m <= 2 ? 1 : 0;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const int64_t yoe = y - (era * 400);
        const int64_t doy = (((153 * (m + (m > 2 ? -3 : 9))) + 2) / 5) + d - 1;
        const int64_t doe = (yoe * 365) + (yoe / 4) - (yoe / 100) + doy;
        return (era * 146097) + doe - 719468;
    }

    std::string toString(__int128 value) {
        if (value == 0)
            return "0";
        const bool negative = value < 0;
        if (negative)
            value = -value;
        std::string out;
        while (value > 0) {
            out.insert(out.begin(), static_cast<char>('0' + static_cast<int>(value % 10)));
            value /= 10;
        }
        return negative ? "-" + out : out;
    }

    struct Value {
        int year; // Oracle year: negative for BC
        int month;
        int day;
        int hour;
        int minute;
        int second;
        uint32_t fraction;

        [[nodiscard]] int64_t expectedEpoch() const {
            const int64_t astronomical = year < 0 ? year + 1 : year;
            return (daysFromCivil(astronomical, month, day) * 86400) + (hour * 3600) + (minute * 60) + second;
        }

        [[nodiscard]] std::string expectedNano() const {
            return toString((static_cast<__int128>(expectedEpoch()) * 1000000000) + fraction);
        }

        [[nodiscard]] std::string label() const {
            char buffer[64];
            snprintf(buffer, sizeof(buffer), "%s%04d-%02d-%02d %02d:%02d:%02d.%09u", year < 0 ? "-" : "", year < 0 ? -year : year, month, day, hour,
                     minute, second, fraction);
            return buffer;
        }

        // Oracle DATE/TIMESTAMP bytes: excess-100 century and year (100 minus the value for BC), the rest excess-1
        void encode(uint8_t* data) const {
            const int absolute = year < 0 ? -year : year;
            data[0] = static_cast<uint8_t>(year < 0 ? 100 - (absolute / 100) : 100 + (absolute / 100));
            data[1] = static_cast<uint8_t>(year < 0 ? 100 - (absolute % 100) : 100 + (absolute % 100));
            data[2] = static_cast<uint8_t>(month);
            data[3] = static_cast<uint8_t>(day);
            data[4] = static_cast<uint8_t>(hour + 1);
            data[5] = static_cast<uint8_t>(minute + 1);
            data[6] = static_cast<uint8_t>(second + 1);
            data[7] = static_cast<uint8_t>(fraction >> 24);
            data[8] = static_cast<uint8_t>(fraction >> 16);
            data[9] = static_cast<uint8_t>(fraction >> 8);
            data[10] = static_cast<uint8_t>(fraction);
        }
    };

    const Value values[] = {
        {-1, 1, 1, 0, 0, 0, 0},
        {-1, 1, 1, 0, 0, 0, 250000000},
        {-1, 2, 29, 12, 0, 0, 0},
        {-1, 3, 1, 0, 0, 0, 0},
        {-1, 12, 31, 23, 59, 59, 999999999},
        {-2, 1, 1, 0, 0, 0, 0},
        {-4, 3, 1, 0, 0, 0, 0},
        {-5, 2, 29, 6, 30, 0, 0},
        {-5, 3, 1, 0, 0, 0, 0},
        {-44, 3, 15, 12, 30, 45, 123456789},
        {-100, 3, 1, 0, 0, 0, 0},
        {-101, 3, 1, 0, 0, 0, 0},
        {-400, 3, 1, 0, 0, 0, 0},
        {-401, 3, 1, 0, 0, 0, 0},
        {-401, 2, 29, 0, 0, 0, 0},
        {-4712, 1, 1, 0, 0, 0, 0},
        {-4712, 12, 31, 23, 59, 59, 500000000},
        {1, 1, 1, 0, 0, 0, 0},
        {1500, 2, 28, 10, 11, 12, 1},
        {1970, 1, 1, 0, 0, 0, 1},
        {2024, 2, 29, 19, 34, 38, 123456789},
    };
}

int main() {
    using namespace OpenLogReplicator;

    for (const Value& value: values) {
        const time_t epoch = Data::valuesToEpoch(value.year, value.month - 1, value.day - 1, value.hour, value.minute, value.second, 0);
        check(value.label() + ": epoch", std::to_string(epoch), std::to_string(value.expectedEpoch()));

        char buffer[32];
        const uint64_t length = Data::epochToIso8601(value.expectedEpoch(), buffer, false, false);
        check(value.label() + ": ISO 8601 text", std::string(buffer, length), value.label().substr(0, value.label().size() - 10));
    }

    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                  Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                  Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO_STRING,
                  Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                  Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                  Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);
    BuilderJson builder(&ctx, &locales, &metadata, format, 0);
    builder.initialize();

    const struct {
        const char* name;
        SysCol::COLTYPE type;
    } types[] = {
        {"TIMESTAMP", SysCol::COLTYPE::TIMESTAMP},
        {"TIMESTAMP WITH LOCAL TIME ZONE", SysCol::COLTYPE::TIMESTAMP_WITH_LOCAL_TZ},
        {"TIMESTAMP WITH TIME ZONE", SysCol::COLTYPE::TIMESTAMP_WITH_TZ},
    };

    for (const auto& type: types) {
        const std::unique_ptr<DbTable> table(new DbTable(1001, 1001, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "T"));
        table->addColumn(new DbColumn(1, -1, 1, "C", type.type, 13, -1, -1, 0, 0, true, false, false, false, false, false, false, false, false));
        const bool withTz = type.type == SysCol::COLTYPE::TIMESTAMP_WITH_TZ;

        for (const Value& value: values) {
            uint8_t data[13];
            value.encode(data);
            // +00:00
            data[11] = 20;
            data[12] = 60;
            const std::string json = BuilderTestAccess::column(builder, table.get(), data, withTz ? 13 : 11);
            check(std::string(type.name) + " " + value.label() + ": JSON", json,
                  R"("C":")" + value.expectedNano() + (withTz ? ",+00:00" : "") + "\"");
        }
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
