/* Test for the epoch number formats of TIMESTAMP and TIMESTAMP WITH TIME ZONE columns in JSON
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

#include <ctime>
#include <iostream>
#include <string>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// A timestamp is seconds since the epoch (floor) plus a fraction in nanoseconds. Values that do not fit in int64 as
// nanoseconds are written in two parts, and a negative value with a fraction borrows one second from the seconds part
// (1938-04-24 22:13:20.5 UTC is -999999999500000000).
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        static std::string json(Builder& builder, Format::TIMESTAMP_FORMAT timestampFormat, time_t timestamp, uint64_t fraction) {
            builder.format.timestampFormat = timestampFormat;
            const uint64_t start = builder.lastBuilderSize + builder.messagePosition;
            builder.columnTimestamp("C", timestamp, fraction);
            return value(builder, start);
        }

        static std::string jsonTz(Builder& builder, Format::TIMESTAMP_TZ_FORMAT timestampTzFormat, time_t timestamp, uint64_t fraction) {
            builder.format.timestampTzFormat = timestampTzFormat;
            const uint64_t start = builder.lastBuilderSize + builder.messagePosition;
            builder.columnTimestampTz("C", timestamp, fraction, "UTC");
            return value(builder, start);
        }

    private:
        // The value of the column: what follows "C":
        static std::string value(Builder& builder, uint64_t start) {
            std::string json(reinterpret_cast<const char*>(builder.lastBuilderQueue->data) + start,
                             builder.lastBuilderSize + builder.messagePosition - start);
            const size_t colon = json.find(':');
            return colon == std::string::npos ? json : json.substr(colon + 1);
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
            std::cout << "ok   " << name << " = " << actual << "\n";
    }

    class TestThread final : public OpenLogReplicator::Thread {
    public:
        explicit TestThread(OpenLogReplicator::Ctx* newCtx): Thread(newCtx, "test") {}
        void run() override {}
        std::string getName() const override { return "test"; }
    };

    struct Case {
        const char* label;
        time_t timestamp;
        uint64_t fraction;
        const char* nano;
        const char* micro;
        const char* milli;
        const char* seconds;
    };

    // Expected values are the exact sum seconds * 10^9 + fraction, rounded half up for the coarser units
    const Case cases[] = {
        {"epoch", 0, 0, "0", "0", "0", "0"},
        {"2024-04-05", 1712345678, 123456789, "1712345678123456789", "1712345678123457", "1712345678123", "1712345678"},
        {"just before the big branch", 999999999, 999999999, "999999999999999999", "1000000000000000", "1000000000000", "1000000000"},
        {"first big positive", 1000000000, 0, "1000000000000000000", "1000000000000000", "1000000000000", "1000000000"},
        {"big positive with fraction", 1000000000, 1, "1000000000000000001", "1000000000000000", "1000000000000", "1000000000"},
        {"9999-12-31 23:59:59.999999999", 253402300799, 999999999, "253402300799999999999", "253402300800000000", "253402300800000",
         "253402300800"},
        {"half a second before the epoch", -1, 500000000, "-500000000", "-500000", "-500", "0"},
        {"small negative", -999999999, 1, "-999999998999999999", "-999999999000000", "-999999999000", "-999999999"},
        {"1938-04-24 22:13:20 exact", -1000000000, 0, "-1000000000000000000", "-1000000000000000", "-1000000000000", "-1000000000"},
        {"1938-04-24 22:13:20.5", -1000000000, 500000000, "-999999999500000000", "-999999999500000", "-999999999500", "-999999999"},
        {"1938-04-24 22:13:20.000000001", -1000000000, 1, "-999999999999999999", "-1000000000000000", "-1000000000000", "-1000000000"},
        {"1906-08-16 at a multiple of 1e9 with fraction", -2000000000, 1, "-1999999999999999999", "-2000000000000000", "-2000000000000",
         "-2000000000"},
        {"negative non-multiple with fraction", -1500000000, 500000000, "-1499999999500000000", "-1499999999500000", "-1499999999500",
         "-1499999999"},
        {"negative with borrow into the seconds", -1000000001, 999999999, "-1000000000000000001", "-1000000000000000", "-1000000000000",
         "-1000000000"},
        {"-4712-01-01", -210866803200, 0, "-210866803200000000000", "-210866803200000000", "-210866803200000", "-210866803200"},
        {"-4712-01-01 plus 1 ns", -210866803200, 1, "-210866803199999999999", "-210866803200000000", "-210866803200000", "-210866803200"},
        {"negative multiple of 1e9 seconds, 1e12", -1000000000000, 250000000, "-999999999999750000000", "-999999999999750000",
         "-999999999999750", "-1000000000000"},
    };
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                  Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                  Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                  Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                  Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                  Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);
    BuilderJson builder(&ctx, &locales, &metadata, format, 0);
    builder.initialize();

    using TF = Format::TIMESTAMP_FORMAT;
    using TZF = Format::TIMESTAMP_TZ_FORMAT;
    for (const Case& c: cases) {
        const std::string label(c.label);
        const std::string q("\"");
        check(label + ": UNIX_NANO", BuilderTestAccess::json(builder, TF::UNIX_NANO, c.timestamp, c.fraction), c.nano);
        check(label + ": UNIX_NANO_STRING", BuilderTestAccess::json(builder, TF::UNIX_NANO_STRING, c.timestamp, c.fraction), q + c.nano + q);
        check(label + ": TZ UNIX_NANO_STRING", BuilderTestAccess::jsonTz(builder, TZF::UNIX_NANO_STRING, c.timestamp, c.fraction),
              q + c.nano + ",UTC" + q);
        check(label + ": UNIX_MICRO", BuilderTestAccess::json(builder, TF::UNIX_MICRO, c.timestamp, c.fraction), c.micro);
        check(label + ": UNIX_MICRO_STRING", BuilderTestAccess::json(builder, TF::UNIX_MICRO_STRING, c.timestamp, c.fraction),
              q + c.micro + q);
        check(label + ": TZ UNIX_MICRO_STRING", BuilderTestAccess::jsonTz(builder, TZF::UNIX_MICRO_STRING, c.timestamp, c.fraction),
              q + c.micro + ",UTC" + q);
        check(label + ": UNIX_MILLI", BuilderTestAccess::json(builder, TF::UNIX_MILLI, c.timestamp, c.fraction), c.milli);
        check(label + ": UNIX_MILLI_STRING", BuilderTestAccess::json(builder, TF::UNIX_MILLI_STRING, c.timestamp, c.fraction),
              q + c.milli + q);
        check(label + ": TZ UNIX_MILLI_STRING", BuilderTestAccess::jsonTz(builder, TZF::UNIX_MILLI_STRING, c.timestamp, c.fraction),
              q + c.milli + ",UTC" + q);
        check(label + ": UNIX", BuilderTestAccess::json(builder, TF::UNIX, c.timestamp, c.fraction), c.seconds);
        check(label + ": UNIX_STRING", BuilderTestAccess::json(builder, TF::UNIX_STRING, c.timestamp, c.fraction), q + c.seconds + q);
        check(label + ": TZ UNIX_STRING", BuilderTestAccess::jsonTz(builder, TZF::UNIX_STRING, c.timestamp, c.fraction),
              q + c.seconds + ",UTC" + q);
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
