/* Test for BINARY_FLOAT and BINARY_DOUBLE values in JSON messages
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
#include <limits>
#include <string>

#include <rapidjson/document.h>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// NaN and infinity are written as the JSON strings "NaN", "Infinity" and "-Infinity" (read by Debezium with
// Float.parseFloat / Double.parseDouble); finite values stay JSON numbers.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        // The JSON a column with this value gives: "C":<value>, as BuilderJson::columnFloat/columnDouble write it
        static std::string column(Builder& builder, bool isDouble, double value) {
            const uint64_t start = builder.lastBuilderSize + builder.messagePosition;
            if (isDouble)
                builder.columnDouble("C", value);
            else
                builder.columnFloat("C", value);
            std::string json(reinterpret_cast<const char*>(builder.lastBuilderQueue->data) + start,
                             builder.lastBuilderSize + builder.messagePosition - start);
            // every column after the first starts with the separator
            if (!json.empty() && json[0] == ',')
                json.erase(0, 1);
            return json;
        }
    };
}

namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
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

    // The value goes through the builder, the message is strict JSON, and the column reads back as the expected value
    void roundTrip(OpenLogReplicator::Builder& builder, bool isDouble, const std::string& label, double value, const std::string& expectedString) {
        const std::string name = std::string(isDouble ? "double " : "float ") + label;
        const std::string json = "{" + OpenLogReplicator::BuilderTestAccess::column(builder, isDouble, value) + "}";

        rapidjson::Document document;
        document.Parse(json.c_str(), json.size());
        if (document.HasParseError() || !document.IsObject() || !document.HasMember("C")) {
            std::cerr << "     " << json << "\n";
            check(name + ": the message is valid JSON", false);
            return;
        }
        check(name + ": the message is valid JSON", true);

        const rapidjson::Value& parsed = document["C"];
        if (!expectedString.empty()) {
            const bool same = parsed.IsString() && std::string(parsed.GetString(), parsed.GetStringLength()) == expectedString;
            if (!same)
                std::cerr << "     expected \"" << expectedString << "\" in " << json << "\n";
            check(name + ": the value is the string " + expectedString, same);
            return;
        }

        const bool same = parsed.IsNumber() && (isDouble ? parsed.GetDouble() == value
                                                         : static_cast<float>(parsed.GetDouble()) == static_cast<float>(value));
        if (!same)
            std::cerr << "     expected " << value << " in " << json << "\n";
        check(name + ": the value is the same number", same);
    }
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

    for (const bool isDouble: {false, true}) {
        // The float limits widened to double, so each value is exact in single precision
        const auto limit = [isDouble](double doubleValue, float floatValue) { return isDouble ? doubleValue : static_cast<double>(floatValue); };
        using D = std::numeric_limits<double>;
        using F = std::numeric_limits<float>;
        const double inf = limit(D::infinity(), F::infinity());
        const double nan = limit(D::quiet_NaN(), F::quiet_NaN());
        roundTrip(builder, isDouble, "NaN", nan, "NaN");
        roundTrip(builder, isDouble, "-NaN", -nan, "NaN");
        roundTrip(builder, isDouble, "signalling NaN", limit(D::signaling_NaN(), F::signaling_NaN()), "NaN");
        roundTrip(builder, isDouble, "Infinity", inf, "Infinity");
        roundTrip(builder, isDouble, "-Infinity", -inf, "-Infinity");

        // Finite values are unchanged JSON numbers (-0 is written as -0, which JSON parsers read as the integer 0)
        roundTrip(builder, isDouble, "1.5", 1.5, "");
        roundTrip(builder, isDouble, "-0", -0.0, "");
        roundTrip(builder, isDouble, "max", limit(D::max(), F::max()), "");
        roundTrip(builder, isDouble, "lowest", limit(D::lowest(), F::lowest()), "");
        roundTrip(builder, isDouble, "denorm_min", limit(D::denorm_min(), F::denorm_min()), "");
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
