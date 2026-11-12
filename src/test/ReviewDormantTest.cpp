/* Review probes for the restart past dormant transactions (not part of the branch under review)
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

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/table/SysCol.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../metadata/Schema.h"
#include "../parser/OpCode.h"
#include "../parser/Parser.h"
#include "../parser/Transaction.h"
#include "../parser/TransactionBuffer.h"

using namespace OpenLogReplicator;

namespace OpenLogReplicator {
    struct ParserTestAccess {
        static void dml(Parser& parser, RedoLogRecord* r1, RedoLogRecord* r2) { parser.appendToTransaction(r1, r2); }
        static void commit(Parser& parser, RedoLogRecord* r1) { parser.appendToTransactionCommit(r1); }
    };
}

namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << std::endl;
    }

    class TestThread final : public Thread {
    public:
        explicit TestThread(Ctx* newCtx): Thread(newCtx, "test") {}
        void run() override {}
        std::string getName() const override { return "test"; }
    };

    struct Record {
        RedoLogRecord r{};

        Record(uint16_t opCode, Xid xid, typeObj obj = 0, uint64_t scn = 0) {
            r.clear();
            r.opCode = opCode;
            r.xid = xid;
            r.obj = obj;
            r.dataObj = obj;
            r.scn = Scn(scn);
        }
    };

    template<typename F> std::string captureStderr(F fn) {
        std::ostringstream captured;
        std::streambuf* old = std::cerr.rdbuf(captured.rdbuf());
        fn();
        std::cerr.rdbuf(old);
        return captured.str();
    }

    bool contains(const std::string& s, const std::string& part) {
        return s.find(part) != std::string::npos;
    }

    constexpr typeObj FILTERED = 77;
    constexpr typeObj OTHER = 88;

    Format testFormat() {
        return {Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT};
    }

    struct Env {
        Ctx ctx;
        TestThread thread{&ctx};
        Locales locales;
        std::unique_ptr<Metadata> metadata;
        std::unique_ptr<TransactionBuffer> buffer;
        Format format{testFormat()};
        std::unique_ptr<BuilderJson> builder;
        std::unique_ptr<Parser> parser;
        DbTable* table;

        Env() {
            ctx.parserThread = &thread;
            ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
            metadata = std::make_unique<Metadata>(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
            metadata->firstDataScn = Scn(500);
            table = new DbTable(FILTERED, FILTERED, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "T");
            table->addColumn(new DbColumn(1, -1, 1, "ID", SysCol::COLTYPE::VARCHAR, 10, -1, -1, 0, 1, false, false, false, false, false, false,
                                          false, false, false));
            metadata->schema->tablePartitionMap.insert_or_assign(FILTERED, table);
            buffer = std::make_unique<TransactionBuffer>(&ctx);
            builder = std::make_unique<BuilderJson>(&ctx, &locales, metadata.get(), format, 0);
            builder->initialize();
            parser = std::make_unique<Parser>(&ctx, builder.get(), metadata.get(), buffer.get(), 0, "test");
        }

        ~Env() {
            parser.reset();
            builder.reset();
            buffer->purge();
            for (const auto& [_, sc]: ctx.swapChunks)
                delete sc;
            ctx.swapChunks.clear();
            metadata->schema->tablePartitionMap.clear();
            delete table;
        }

        void dml(Xid xid, uint16_t op, typeObj obj) {
            Record r1(0x0501, xid, obj), r2(op, xid, obj);
            ParserTestAccess::dml(*parser, &r1.r, &r2.r);
        }

        std::string commit(Xid xid, uint64_t scn) {
            Record c(0x0504, xid, 0, scn);
            return captureStderr([&] { ParserTestAccess::commit(*parser, &c.r); });
        }
    };

    const Xid A(5, 1, 100);
    const Xid B(5, 2, 200);
}

int main() {
    {
        // A transaction the cold start of the previous run already named as lost (60011 at startup) is still unknown after
        // a restart past dormant transactions: it was never in the transaction map, so no checkpoint could list it.
        // Its commit is a known loss, not a wrong dormant state: it should not be reported as 60061 with the hint to
        // restart without the option.
        Env env;
        env.metadata->dormantRestart = true;
        env.metadata->coldStartLostXids.insert(A);
        env.dml(A, 0x0B02, FILTERED);
        const std::string err = env.commit(A, 600);
        check("inherited lost transaction after a dormant restart: not blamed on the dormant state (no 60061)", !contains(err, "60061"));
    }
    {
        // Same for a transaction that changed only tables outside the filter: nothing was lost, no warning is due
        Env env;
        env.metadata->dormantRestart = true;
        env.dml(B, 0x0B02, OTHER);
        const std::string err = env.commit(B, 600);
        check("unknown transaction outside the filter after a dormant restart: silent", !contains(err, "60061") && !contains(err, "60011"));
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed (review probes: a failure documents the behaviour, see the review)\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
