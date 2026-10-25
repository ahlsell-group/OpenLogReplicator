/* Test for the escaping of control characters in JSON string values
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
#include <string>

#include <rapidjson/document.h>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// RFC 8259 requires every character below 0x20 in a string to be escaped, and \u takes four hex digits. A VARCHAR2
// value with 0x1F (unit separator) was sent raw, so the client could not parse the message and stopped on it (Debezium
// "Illegal unquoted character (CTRL-CHAR, code 31)"). The \u escapes of the other control characters were written with
// decimal digits, so 0x0B and 0x0E to 0x1E arrived as other characters (0x1E as 0, "0").
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        // The JSON a column with this value gives: "C":"<value>", as BuilderJson::columnString writes it
        static std::string columnString(Builder& builder, const std::string& value) {
            builder.valueSize = 0;
            builder.valueBufferCheck(value.size(), FileOffset(0));
            for (const char c: value)
                builder.valueBufferAppend(static_cast<uint8_t>(c));

            const uint64_t start = builder.lastBuilderSize + builder.messagePosition;
            builder.columnString("C");
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

    std::string printable(const std::string& text) {
        std::string out;
        for (const char c: text) {
            const auto byte = static_cast<unsigned char>(c);
            if (byte < 0x20 || byte == 0x7F) {
                char hex[8];
                snprintf(hex, sizeof(hex), "<%02X>", byte);
                out += hex;
            } else
                out += c;
        }
        return out;
    }

    // The value goes through the builder, and the message parses back to the same bytes
    void roundTrip(OpenLogReplicator::Builder& builder, const std::string& label, const std::string& value) {
        const std::string json = "{" + OpenLogReplicator::BuilderTestAccess::columnString(builder, value) + "}";

        bool raw = false;
        for (const char c: json)
            if (static_cast<unsigned char>(c) < 0x20)
                raw = true;
        check(label + ": no control character is written unescaped", !raw);

        rapidjson::Document document;
        document.Parse(json.c_str(), json.size());
        if (document.HasParseError() || !document.IsObject() || !document.HasMember("C") || !document["C"].IsString()) {
            std::cerr << "     " << printable(json) << "\n";
            check(label + ": the message is valid JSON", false);
            return;
        }
        check(label + ": the message is valid JSON", true);

        const std::string decoded(document["C"].GetString(), document["C"].GetStringLength());
        if (decoded != value)
            std::cerr << "     sent " << printable(value) << ", parsed " << printable(decoded) << " from " << printable(json) << "\n";
        check(label + ": the value parses back unchanged", decoded == value);
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

    // The value that stopped vivaldidata: a VARCHAR2 with a unit separator inside
    roundTrip(builder, "text with 0x1F", std::string("REF") + '\x1f' + "123");

    // Every character JSON requires to be escaped, on its own
    for (int c = 0x00; c < 0x20; ++c) {
        char label[16];
        snprintf(label, sizeof(label), "0x%02X", c);
        roundTrip(builder, label, std::string("a") + static_cast<char>(c) + "b");
    }

    // Characters with their own escapes, and ones that need none
    roundTrip(builder, "quote, backslash, slash", "\"\\/");
    roundTrip(builder, "DEL and UTF-8", std::string("\x7f") + "\xc3\x85\xc3\x84\xc3\x96");

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
