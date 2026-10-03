/* Test for the transaction id in the JSON output with xid format 3 (LogMiner)
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

#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Attribute.h"
#include "../common/Ctx.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/Format.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/table/SysCol.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../metadata/Schema.h"

// xid format 3 must equal V$LOGMNR_CONTENTS.XID of the source database: the raw bytes of usn, slt and sqn in the byte
// order of the database host, which the redo log header tells (Ctx::isBigEndian). Transaction 0x0012.005.0003f2a1 is
// 001200050003f2a1 on a big-endian host (e.g. AIX) and 12000500a1f20300 on a little-endian one. Only the builders'
// public interface is used, so the test runs unchanged on builds without Xid::toRaw.
using namespace OpenLogReplicator;

namespace {
    int failures = 0;

    void check(const std::string& name, const std::string& actual, const std::string& expected) {
        if (actual != expected) {
            std::cerr << "FAIL " << name << ": got " << actual << ", expected " << expected << "\n";
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

    Format makeFormat(Format::XID_FORMAT xidFormat, Format::SCN_TYPE scnType) {
        return {Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                Format::REDO_THREAD_FORMAT::SKIP, xidFormat, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, scnType, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT};
    }

    // Table TEST.T (obj 1001) with one VARCHAR2 primary key column
    DbTable* makeTable() {
        auto* table = new DbTable(1001, 1001, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "T");
        table->addColumn(new DbColumn(1, -1, 1, "ID", SysCol::COLTYPE::VARCHAR, 10, -1, -1, 0, 1, false, false, false, false, false, false,
                                      false, false, false));
        return table;
    }

    // Undo/redo pair of an INSERT into TEST.T without column data
    struct Insert {
        RedoLogRecord undo{};
        RedoLogRecord redo{};
        std::deque<const RedoLogRecord*> redo1;
        std::deque<const RedoLogRecord*> redo2;

        Insert() {
            undo.obj = 1001;
            undo.dataObj = 1001;
            redo.obj = 1001;
            redo.dataObj = 1001;
            redo.fb = RedoLogRecord::FB_F | RedoLogRecord::FB_L;
            redo1.push_back(&undo);
            redo2.push_back(&redo);
        }
    };

    struct Message {
        Scn scn;
        std::string json;
    };

    // Every message the builder queued, in order
    std::vector<Message> messages(const Builder& builder) {
        std::vector<Message> result;
        for (const BuilderQueue* queue = builder.firstBuilderQueue; queue != nullptr; queue = queue->next) {
            uint64_t pos = 0;
            while (pos < queue->confirmedSize) {
                const auto* msg = reinterpret_cast<const BuilderMsg*>(queue->data + pos);
                result.push_back({msg->scn, std::string(reinterpret_cast<const char*>(msg->data), msg->size)});
                pos = static_cast<uint64_t>(msg->data - queue->data) + ((msg->size + 7) & 0xFFFFFFFFFFFFFFF8);
            }
        }
        return result;
    }

    // Value of a top-level field: "key":"text" or "key":number
    std::string field(const std::string& json, const std::string& key) {
        const std::string k = "\"" + key + "\":";
        const std::string::size_type pos = json.find(k);
        if (pos == std::string::npos)
            return "<missing>";
        std::string::size_type start = pos + k.length();
        if (json[start] == '"')
            return json.substr(start + 1, json.find('"', start + 1) - start - 1);
        std::string::size_type end = start;
        while (end < json.size() && json[end] != ',' && json[end] != '}')
            ++end;
        return json.substr(start, end - start);
    }

    // The "xid" of the begin and the commit message of one transaction with one INSERT
    std::vector<std::string> xidsOf(bool bigEndian, Format::XID_FORMAT xidFormat) {
        Ctx ctx;
        TestThread thread(&ctx);
        ctx.parserThread = &thread;
        ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
        if (bigEndian)
            ctx.setBigEndian();
        Locales locales;
        Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
        const std::unique_ptr<DbTable> table(makeTable());
        metadata.schema->tablePartitionMap.insert_or_assign(table->obj, table.get());
        Format format = makeFormat(xidFormat, Format::SCN_TYPE::DEFAULT);
        BuilderJson builder(&ctx, &locales, &metadata, format, 0);
        builder.initialize();

        AttributeMap attributes;
        attributes[Attribute::KEY::LOGIN_USER_NAME] = "TEST";
        const Insert insert;
        builder.processBegin(Xid(0x0012, 0x0005, 0x0003f2a1), 1, Seq(1), Scn(100), Time(0), Seq(1), Scn(110), Time(0), &attributes);
        builder.processDml(Seq(1), Scn(105), Time(0), nullptr, nullptr, insert.redo1, insert.redo2, Format::TRANSACTION_TYPE::INSERT, false,
                           false, false);
        builder.processCommit();

        std::vector<std::string> xids;
        for (const Message& message: messages(builder))
            xids.push_back(field(message.json, "xid"));
        metadata.schema->tablePartitionMap.clear();
        return xids;
    }

    void checkAll(const std::string& name, const std::vector<std::string>& xids, const std::string& expected) {
        check(name + ": begin, insert and commit sent", std::to_string(xids.size()), "3");
        for (uint64_t i = 0; i < xids.size(); ++i)
            check(name + ": message " + std::to_string(i + 1), xids[i], expected);
    }
}

int main() {
    checkAll("little-endian host, format 3", xidsOf(false, Format::XID_FORMAT::TEXT_REVERSED), "12000500a1f20300");
    checkAll("big-endian host, format 3", xidsOf(true, Format::XID_FORMAT::TEXT_REVERSED), "001200050003f2a1");
    // Format 0 does not depend on the byte order
    checkAll("little-endian host, format 0", xidsOf(false, Format::XID_FORMAT::TEXT_HEX), "0x0012.005.0003f2a1");
    checkAll("big-endian host, format 0", xidsOf(true, Format::XID_FORMAT::TEXT_HEX), "0x0012.005.0003f2a1");

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
