/* Test for the warning on DATE and TIMESTAMP values that can't be decoded (code 60043)
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
#include <sstream>
#include <string>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// A DATE or TIMESTAMP value that can't be decoded (wrong length, month 0, a field out of range) is sent as "?", which
// Debezium turns into NULL or epoch 0; every such value logs warning 60043 with the file offset, table, column and raw bytes.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        // The JSON processValue writes for column col of the table
        static std::string processValue(Builder& builder, const DbTable* table, typeCol col, const std::vector<uint8_t>& data,
                                        FileOffset fileOffset) {
            const uint64_t start = builder.lastBuilderSize + builder.messagePosition;
            builder.processValue(nullptr, nullptr, table, col, data.data(), static_cast<uint32_t>(data.size()), fileOffset, true, false);
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

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cout << "FAIL " << name << "\n";
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

    struct Result {
        std::string json;
        std::string log;
    };

    // The JSON of the column and what was logged to stderr while writing it
    Result run(OpenLogReplicator::Builder& builder, const OpenLogReplicator::DbTable* table, typeCol col,
               const std::vector<uint8_t>& data, uint64_t offset) {
        std::ostringstream captured;
        std::streambuf* old = std::cerr.rdbuf(captured.rdbuf());
        Result result;
        result.json = OpenLogReplicator::BuilderTestAccess::processValue(builder, table, col, data, OpenLogReplicator::FileOffset(offset));
        std::cerr.rdbuf(old);
        result.log = captured.str();
        return result;
    }

    bool contains(const std::string& text, const std::string& part) {
        return text.find(part) != std::string::npos;
    }

    // The value is still sent as "?", and one warning 60043 names the offset, owner.table, column and bytes
    void expectWarning(OpenLogReplicator::Builder& builder, const OpenLogReplicator::DbTable* table, typeCol col,
                       const std::string& column, const std::string& label, const std::vector<uint8_t>& data, uint64_t offset,
                       const std::string& hex) {
        const Result result = run(builder, table, col, data, offset);
        check(label + ": value is sent as \"?\"", result.json == "\"" + column + "\":\"?\"");
        const bool logged = contains(result.log, "WARN  60043 ") && contains(result.log, "offset: " + std::to_string(offset)) &&
                contains(result.log, "table: TEST.T") && contains(result.log, "column: " + column) && contains(result.log, hex);
        if (!logged)
            std::cout << "     json: " << result.json << "\n     log: " << result.log << "\n";
        check(label + ": warning 60043 with offset, table, column and bytes", logged);
    }

    void expectNoWarning(OpenLogReplicator::Builder& builder, const OpenLogReplicator::DbTable* table, typeCol col,
                         const std::string& label, const std::vector<uint8_t>& data) {
        const Result result = run(builder, table, col, data, 100);
        check(label + ": decoded, not \"?\"", !contains(result.json, "\"?\""));
        check(label + ": no warning", !contains(result.log, "60043"));
    }
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    locales.initialize();
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                  Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                  Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                  Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                  Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                  Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);
    BuilderJson builder(&ctx, &locales, &metadata, format, 0);
    builder.initialize();

    // TEST.T: D DATE, TS TIMESTAMP, LTZ TIMESTAMP WITH LOCAL TIME ZONE, TZ TIMESTAMP WITH TIME ZONE
    DbTable table(1001, 1001, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "T");
    table.addColumn(new DbColumn(1, -1, 1, "D", SysCol::COLTYPE::DATE, 7, -1, -1, 0, 0, true, false, false, false, false, false, false, false,
                                 false));
    table.addColumn(new DbColumn(2, -1, 2, "TS", SysCol::COLTYPE::TIMESTAMP, 11, -1, -1, 0, 0, true, false, false, false, false, false, false,
                                 false, false));
    table.addColumn(new DbColumn(3, -1, 3, "LTZ", SysCol::COLTYPE::TIMESTAMP_WITH_LOCAL_TZ, 11, -1, -1, 0, 0, true, false, false, false, false,
                                 false, false, false, false));
    table.addColumn(new DbColumn(4, -1, 4, "TZ", SysCol::COLTYPE::TIMESTAMP_WITH_TZ, 13, -1, -1, 0, 0, true, false, false, false, false, false,
                                 false, false, false));

    // 2024-01-15 10:20:30
    expectNoWarning(builder, &table, 0, "valid DATE", {0x78, 0x7c, 0x01, 0x0f, 0x0b, 0x15, 0x1f});
    expectNoWarning(builder, &table, 1, "valid TIMESTAMP", {0x78, 0x7c, 0x01, 0x0f, 0x0b, 0x15, 0x1f, 0x00, 0x00, 0x00, 0x01});

    expectWarning(builder, &table, 0, "D", "DATE with month 0", {0x78, 0x7c, 0x00, 0x0f, 0x0b, 0x15, 0x1f}, 123456,
                  "78 7c 00 0f 0b 15 1f");
    expectWarning(builder, &table, 0, "D", "DATE with hour 24", {0x78, 0x7c, 0x01, 0x0f, 0x19, 0x15, 0x1f}, 7,
                  "78 7c 01 0f 19 15 1f");
    expectWarning(builder, &table, 0, "D", "DATE of 5 bytes", {0x78, 0x7c, 0x01, 0x0f, 0x0b}, 4096, "78 7c 01 0f 0b");
    expectWarning(builder, &table, 1, "TS", "TIMESTAMP with fraction above 999999999",
                  {0x78, 0x7c, 0x01, 0x0f, 0x0b, 0x15, 0x1f, 0x3b, 0x9a, 0xca, 0x00}, 99, "78 7c 01 0f 0b 15 1f 3b 9a ca 00");
    expectWarning(builder, &table, 2, "LTZ", "TIMESTAMP WITH LOCAL TIME ZONE with day 0", {0x78, 0x7c, 0x01, 0x00, 0x0b, 0x15, 0x1f}, 512,
                  "78 7c 01 00 0b 15 1f");
    expectWarning(builder, &table, 3, "TZ", "TIMESTAMP WITH TIME ZONE with minute 60",
                  {0x78, 0x7c, 0x01, 0x0f, 0x0b, 0x3d, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x14, 0x3c}, 1024,
                  "78 7c 01 0f 0b 3d 1f 00 00 00 00 14 3c");

    if (failures > 0) {
        std::cout << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
