/* Test for the warning about column values that are not sent because of their type
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

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/table/SysCol.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// With unknown-type HIDE the builder leaves out LONG, LONG RAW, ROWID, XMLTYPE and (without EXPERIMENTAL_JSON) JSON
// values and logs warning 60043 once per table and column the first time it drops one.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        // The bytes processValue appends to the message for this value
        static std::string processValue(Builder& builder, const DbTable* table, typeCol col, const uint8_t* data, uint32_t size) {
            const uint64_t start = builder.lastBuilderSize + builder.messagePosition;
            builder.processValue(nullptr, nullptr, table, col, data, size, FileOffset(0), true, false);
            return {reinterpret_cast<const char*>(builder.lastBuilderQueue->data) + start,
                    builder.lastBuilderSize + builder.messagePosition - start};
        }
    };
}

using namespace OpenLogReplicator;

namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    class TestThread final : public Thread {
    public:
        explicit TestThread(Ctx* newCtx): Thread(newCtx, "test") {}
        void run() override {}
        std::string getName() const override { return "test"; }
    };

    // ROWID is not a SysCol::COLTYPE value, the dictionary still gives it type 69
    constexpr auto COLTYPE_ROWID = static_cast<SysCol::COLTYPE>(69);

    DbTable* makeTable(typeObj obj, const std::string& name) {
        auto* table = new DbTable(obj, obj, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", name);
        const std::pair<std::string, SysCol::COLTYPE> columns[]{
                {"ID", SysCol::COLTYPE::NUMBER},
                {"C_LONG", SysCol::COLTYPE::LONG},
                {"C_LONG_RAW", SysCol::COLTYPE::LONG_RAW},
                {"C_ROWID", COLTYPE_ROWID},
                {"C_XML", SysCol::COLTYPE::XMLTYPE},
                {"C_JSON", SysCol::COLTYPE::JSON}};
        typeCol i = 0;
        for (const auto& [columnName, type]: columns) {
            table->addColumn(new DbColumn(i + 1, -1, i + 1, columnName, type, 10, -1, -1, 0, i == 0 ? 1 : 0, i != 0, false, false, false,
                                          false, false, false, false, false));
            ++i;
        }
        return table;
    }

    size_t count(const std::string& text, const std::string& what) {
        size_t n = 0;
        for (size_t pos = text.find(what); pos != std::string::npos; pos = text.find(what, pos + what.size()))
            ++n;
        return n;
    }
}

int main() {
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

    DbTable* t1 = makeTable(1001, "T1");
    DbTable* t2 = makeTable(1002, "T2");

    // NUMBER 1, and a few bytes for the dropped columns (their content is not looked at)
    static constexpr uint8_t number[2]{0xC1, 0x02};
    static constexpr uint8_t other[4]{0x41, 0x42, 0x43, 0x44};

    std::ostringstream log;
    std::streambuf* cerr = std::cerr.rdbuf(log.rdbuf());
    std::string idOut;
    std::string hiddenOut;
    for (int round = 0; round < 3; ++round) {
        for (DbTable* table: {t1, t2}) {
            idOut += BuilderTestAccess::processValue(builder, table, 0, number, sizeof(number));
            for (typeCol col = 1; col < 6; ++col)
                hiddenOut += BuilderTestAccess::processValue(builder, table, col, other, sizeof(other));
        }
    }
    std::cerr.rdbuf(cerr);
    const std::string text = log.str();
    std::cout << text;

    check("NUMBER column is sent", idOut == R"("ID":1,"ID":1,"ID":1,"ID":1,"ID":1,"ID":1)");
    check("the hidden columns add nothing to the message", hiddenOut.empty());
    check("one warning 60043 per table and hidden column (10)", count(text, "WARN  60043 ") == 10);
    check("no warning for the NUMBER column", count(text, "column ID") == 0 && count(text, ".ID ") == 0);
    for (const char* table: {"TEST.T1", "TEST.T2"}) {
        const std::string name(table);
        check(name + ".C_LONG named once with type LONG", count(text, name + ".C_LONG type LONG (8)") == 1);
        check(name + ".C_LONG_RAW named once with type LONG RAW", count(text, name + ".C_LONG_RAW type LONG RAW (24)") == 1);
        check(name + ".C_ROWID named once with type ROWID", count(text, name + ".C_ROWID type ROWID (69)") == 1);
        check(name + ".C_XML named once with type XMLTYPE", count(text, name + ".C_XML type XMLTYPE (58)") == 1);
        check(name + ".C_JSON named once with type JSON", count(text, name + ".C_JSON type JSON (119)") == 1);
    }

    delete t1;
    delete t2;

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
