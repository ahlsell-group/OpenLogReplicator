/* Test for merging a column value split over several row pieces
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

#include "../builder/Builder.h"
#include "../common/Ctx.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/Format.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/exception/RedoLogException.h"
#include "../common/table/SysCol.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../metadata/Schema.h"

// A column of a chained row can be split over several row pieces: the first part has FB_N, every middle part FB_P|FB_N
// and the last part FB_P. Builder::processDml concatenates the parts in the order valueSet received them before it
// builds the message, so every middle part must survive, not only the last one.
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

    // Records the merged before and after image of column 1 handed to processUpdate, writes no output
    class TestBuilder final : public Builder {
    public:
        uint64_t updates{0};
        bool failUpdate{false};
        std::string before;
        std::string after;

        TestBuilder(Ctx* newCtx, Locales* newLocales, Metadata* newMetadata, const Format& newFormat):
                Builder(newCtx, newLocales, newMetadata, newFormat, 0) {}

        void setPart(Format::VALUE_TYPE type, uint16_t column, const std::string& data, uint8_t fb) {
            valueSet(type, column, reinterpret_cast<const uint8_t*>(data.data()), data.size(), fb, false);
        }

    protected:
        void columnFloat(const std::string&, double) override {}
        void columnDouble(const std::string&, long double) override {}
        void columnString(const std::string&) override {}
        void columnNumber(const std::string&, int, int) override {}
        void columnRaw(const std::string&, const uint8_t*, uint64_t) override {}
        void columnRowId(const std::string&, RowId) override {}
        void columnTimestamp(const std::string&, time_t, uint64_t) override {}
        void columnTimestampTz(const std::string&, time_t, uint64_t, const std::string_view&) override {}
        void processInsert(Seq, Scn, Time, LobCtx*, const XmlCtx*, const DbTable*, typeObj, typeDataObj, typeDba, typeSlot, FileOffset) override {}
        void processUpdate(Seq, Scn, Time, LobCtx*, const XmlCtx*, const DbTable*, typeObj, typeDataObj, typeDba, typeSlot, FileOffset) override {
            if (failUpdate)
                throw RedoLogException(50009, "simulated error while building the message");
            ++updates;
            before = image(Format::VALUE_TYPE::BEFORE);
            after = image(Format::VALUE_TYPE::AFTER);
        }
        void processDelete(Seq, Scn, Time, LobCtx*, const XmlCtx*, const DbTable*, typeObj, typeDataObj, typeDba, typeSlot, FileOffset) override {}
        void processDdl(Seq, Scn, Time, const DbTable*, typeObj) override {}
        void processBeginMessage(Seq, Time) override {}

    public:
        void processCommit() override {}
        void processCheckpoint(Seq, Scn, Time, FileOffset, bool) override {}

    private:
        std::string image(Format::VALUE_TYPE type) const {
            if (values[1][+type] == nullptr)
                return "<null>";
            return {reinterpret_cast<const char*>(values[1][+type]), static_cast<size_t>(sizes[1][+type])};
        }
    };

    DbTable* makeTable(typeObj obj) {
        auto* table = new DbTable(obj, obj, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "CHAINED");
        table->addColumn(new DbColumn(1, -1, 1, "ID", SysCol::COLTYPE::VARCHAR, 10, -1, -1, 0, 1, false, false, false, false, false, false, false,
                                      false, false));
        table->addColumn(new DbColumn(2, -1, 2, "TXT", SysCol::COLTYPE::VARCHAR, 4000, -1, -1, 0, 0, true, false, false, false, false, false, false,
                                      false, false));
        return table;
    }

    // An undo/redo pair of a row change without column data; the column parts are set directly with valueSet
    struct RowChange {
        RedoLogRecord undo{};
        RedoLogRecord redo{};
        std::deque<const RedoLogRecord*> redo1;
        std::deque<const RedoLogRecord*> redo2;

        explicit RowChange(typeObj obj) {
            undo.obj = obj;
            undo.dataObj = obj;
            redo.obj = obj;
            redo.dataObj = obj;
            redo.fb = RedoLogRecord::FB_F | RedoLogRecord::FB_L;
            redo1.push_back(&undo);
            redo2.push_back(&redo);
        }
    };

    Format makeFormat() {
        return {Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT};
    }

    // Flags of part i of n: first FB_N, middle FB_P|FB_N, last FB_P
    uint8_t partFlags(size_t i, size_t n) {
        uint8_t fb = 0;
        if (i > 0)
            fb |= RedoLogRecord::FB_P;
        if (i + 1 < n)
            fb |= RedoLogRecord::FB_N;
        return fb;
    }

    // An UPDATE whose before and after image of column TXT come in parts, merged by processDml
    void run(TestBuilder& builder, const std::vector<std::string>& beforeParts, const std::vector<std::string>& afterParts) {
        std::string expectBefore;
        std::string expectAfter;
        for (size_t i = 0; i < beforeParts.size(); ++i) {
            builder.setPart(Format::VALUE_TYPE::BEFORE, 1, beforeParts[i], partFlags(i, beforeParts.size()));
            expectBefore += beforeParts[i];
        }
        for (size_t i = 0; i < afterParts.size(); ++i) {
            builder.setPart(Format::VALUE_TYPE::AFTER, 1, afterParts[i], partFlags(i, afterParts.size()));
            expectAfter += afterParts[i];
        }

        const std::string label = std::to_string(beforeParts.size()) + "/" + std::to_string(afterParts.size()) + " parts";
        const uint64_t updates = builder.updates;
        const RowChange upd(1001);
        try {
            builder.processDml(Seq(1), Scn(100), Time(0), nullptr, nullptr, upd.redo1, upd.redo2, Format::TRANSACTION_TYPE::UPDATE, false, false,
                               false);
            check(label + ": the UPDATE is sent", builder.updates == updates + 1);
        } catch (RedoLogException& ex) {
            std::cerr << "     " << ex.code << " " << ex.msg << "\n";
            check(label + ": the UPDATE is sent", false);
            return;
        }
        check(label + ": before image is " + expectBefore + " (got " + builder.before + ")", builder.before == expectBefore);
        check(label + ": after image is " + expectAfter + " (got " + builder.after + ")", builder.after == expectAfter);
    }

    // An UPDATE that throws in processDml, as under IGNORE_DATA_ERRORS the parser then goes on with the next row
    void runAborted(TestBuilder& builder, const std::string& label) {
        const RowChange upd(1001);
        try {
            builder.processDml(Seq(1), Scn(100), Time(0), nullptr, nullptr, upd.redo1, upd.redo2, Format::TRANSACTION_TYPE::UPDATE, false, false,
                               false);
            check(label + ": the row is aborted", false);
        } catch (RedoLogException&) {
            check(label + ": the row is aborted", true);
        }
    }
}

int main() {
    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    const std::unique_ptr<DbTable> table(makeTable(1001));
    metadata.schema->tablePartitionMap.insert_or_assign(table->obj, table.get());
    TestBuilder builder(&ctx, &locales, &metadata, makeFormat());
    builder.initialize();

    run(builder, {"AB", "CD"}, {"ab", "cd"});
    run(builder, {"AB", "CD", "EF"}, {"ab", "cd", "ef"});
    run(builder, {"AB", "CD", "EF", "GH"}, {"ab", "cd", "ef", "gh"});
    run(builder, {"AB", "CD", "EF", "GH", "IJ"}, {"ab", "cd", "ef", "gh", "ij"});
    // the next row starts clean after a row with many parts
    run(builder, {"AB", "CD", "EF"}, {"ab", "cd", "ef", "gh", "ij", "kl"});
    run(builder, {"XY"}, {"xy"});

    // the parts point into the redo record, which outlives the row
    const std::vector<std::string> abortedParts{"PP", "QQ", "RR", "SS"};
    const std::vector<std::string> idParts{"I1", "I2"};

    // a row aborted while its message is built, after its columns were merged, leaves nothing to the next row
    for (size_t i = 0; i < abortedParts.size(); ++i)
        builder.setPart(Format::VALUE_TYPE::BEFORE, 1, abortedParts[i], partFlags(i, abortedParts.size()));
    builder.failUpdate = true;
    runAborted(builder, "abort after merge");
    builder.failUpdate = false;
    run(builder, {"AB", "CD", "EF"}, {"ab", "cd", "ef"});

    // a row aborted before its columns were merged (ID has a value and a part) leaves no parts to the next row
    builder.setPart(Format::VALUE_TYPE::BEFORE, 0, idParts[0], 0);
    builder.setPart(Format::VALUE_TYPE::BEFORE, 0, idParts[1], RedoLogRecord::FB_N);
    for (size_t i = 0; i < abortedParts.size(); ++i)
        builder.setPart(Format::VALUE_TYPE::BEFORE, 1, abortedParts[i], partFlags(i, abortedParts.size()));
    runAborted(builder, "abort before merge");
    run(builder, {"AB", "CD", "EF"}, {"ab", "cd", "ef"});
    run(builder, {"XY"}, {"xy"});

    metadata.schema->tablePartitionMap.clear();
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
