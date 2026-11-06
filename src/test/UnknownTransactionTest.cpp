/* Test for changes of transactions OLR has not seen begin (WARN 60011 without flag 1024)
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

#include "../common/Ctx.h"
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

// Without flag 1024 a change of a transaction whose begin OLR did not read is dropped together with the transaction. It is
// a loss only when the change is one OLR sends and the commit is: then 60011 names the transaction at its commit.
using namespace OpenLogReplicator;

namespace OpenLogReplicator {
    struct ParserTestAccess {
        static void dml(Parser& parser, RedoLogRecord* r1, RedoLogRecord* r2) { parser.appendToTransaction(r1, r2); }
        static void single(Parser& parser, RedoLogRecord* r1) { parser.appendToTransaction(r1); }
        static void ddl(Parser& parser, RedoLogRecord* r1) { parser.appendToTransactionDdl(r1); }
        static void rollback(Parser& parser, RedoLogRecord* r1) { parser.appendToTransactionRollback(r1); }
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

    int count(const std::string& s, const std::string& part) {
        int n = 0;
        for (size_t p = s.find(part); p != std::string::npos; p = s.find(part, p + 1))
            ++n;
        return n;
    }

    constexpr typeObj FILTERED = 77;
    constexpr typeObj OTHER = 88;

    struct Env {
        Ctx ctx;
        TestThread thread{&ctx};
        Locales locales;
        std::unique_ptr<Metadata> metadata;
        std::unique_ptr<TransactionBuffer> buffer;
        std::unique_ptr<Parser> parser;
        DbTable* table;

        explicit Env(uint flags = 0) {
            ctx.parserThread = &thread;
            ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
            ctx.flags = flags;
            metadata = std::make_unique<Metadata>(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
            metadata->firstDataScn = Scn(500);
            table = new DbTable(FILTERED, FILTERED, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "T");
            table->addColumn(new DbColumn(1, -1, 1, "ID", SysCol::COLTYPE::VARCHAR, 10, -1, -1, 0, 1, false, false, false, false, false, false,
                                          false, false, false));
            metadata->schema->tablePartitionMap.insert_or_assign(FILTERED, table);
            buffer = std::make_unique<TransactionBuffer>(&ctx);
            parser = std::make_unique<Parser>(&ctx, nullptr, metadata.get(), buffer.get(), 0, "test");
        }

        ~Env() {
            parser.reset();
            buffer->purge();
            for (const auto& [_, sc]: ctx.swapChunks)
                delete sc;
            ctx.swapChunks.clear();
            metadata->schema->tablePartitionMap.clear();
            delete table;
        }

        void dml(Xid xid, uint16_t op, typeObj obj, typeConId conId = 0) {
            Record r1(0x0501, xid, obj), r2(op, xid, obj);
            r1.r.conId = conId;
            r2.r.conId = conId;
            ParserTestAccess::dml(*parser, &r1.r, &r2.r);
        }

        std::string commit(Xid xid, uint64_t scn, bool rollback = false, typeConId conId = 0) {
            Record c(0x0504, xid, 0, scn);
            c.r.conId = conId;
            if (rollback)
                c.r.flg = OpCode::FLG_ROLLBACK_OP0504;
            return captureStderr([&] { ParserTestAccess::commit(*parser, &c.r); });
        }
    };

    const Xid A(5, 1, 100);
    const Xid B(5, 2, 200);
}

int main() {
    {
        Env env;
        env.dml(A, 0x0B02, FILTERED);
        env.dml(A, 0x0B05, FILTERED);
        const std::string err = env.commit(A, 600);
        check("filtered rows, commit after the first data scn: 60011 once, names the xid",
              count(err, "WARN  60011") == 1 && err.find(A.toString()) != std::string::npos);
        check("reported once: a second commit record of the xid is silent", count(env.commit(A, 601), "60011") == 0);
    }
    {
        Env env;
        env.dml(A, 0x0B02, OTHER);
        check("rows of a table outside the filter only: silent", count(env.commit(A, 600), "60011") == 0);
    }
    {
        Env env;
        env.dml(A, 0x0513, 0);
        env.dml(A, 0x0514, 0);
        check("session information only: silent", count(env.commit(A, 600), "60011") == 0);
    }
    {
        Env env;
        check("bare commit, nothing seen: silent", count(env.commit(A, 600), "60011") == 0);
    }
    {
        Env env;
        env.dml(A, 0x0B02, FILTERED);
        check("rollback: silent", count(env.commit(A, 600, true), "60011") == 0);
        env.dml(B, 0x0B02, FILTERED);
        check("commit before the first data scn (not sent anyway): silent", count(env.commit(B, 499), "60011") == 0);
    }
    {
        Env env;
        env.metadata->coldStartLostXids.insert(A);
        env.dml(A, 0x0B02, FILTERED);
        check("named by the cold start already: silent", count(env.commit(A, 600), "60011") == 0);
    }
    {
        Env env;
        env.buffer->skipXidList.insert(A);
        env.dml(A, 0x0B02, FILTERED);
        check("skip-xid: silent", count(env.commit(A, 600), "60011") == 0 && env.buffer->unknownXids.empty());
    }
    {
        Env env;
        env.metadata->conId = 3;
        env.dml(A, 0x0B02, FILTERED, 1);
        check("other container: silent", count(env.commit(A, 600, false, 1), "60011") == 0);
    }
    {
        Env env;
        // same slot, another sequence number: tracked per xid
        const Xid reuse(5, 1, 101);
        env.dml(A, 0x0B02, FILTERED);
        check("slot reuse: commit of the other xid is silent", count(env.commit(reuse, 600), "60011") == 0);
        check("slot reuse: the xid with rows is reported", count(env.commit(A, 601), "60011") == 1);
    }
    {
        Env env;
        Record split(0x0501, A, FILTERED);
        split.r.opc = 0x0B01;
        split.r.flg = OpCode::FLG_MULTIBLOCKUNDOTAIL;
        ParserTestAccess::single(*env.parser, &split.r);
        check("single 5.1 (split undo) of a filtered table: reported", count(env.commit(A, 600), "60011") == 1);
    }
    {
        Env env(static_cast<uint>(Ctx::REDO_FLAGS::SHOW_DDL));
        Record ddl(0x1801, A, OTHER);
        ParserTestAccess::ddl(*env.parser, &ddl.r);
        check("DDL with SHOW_DDL: reported", count(env.commit(A, 600), "60011") == 1);
        Env plain;
        Record ddl2(0x1801, A, OTHER);
        ParserTestAccess::ddl(*plain.parser, &ddl2.r);
        check("DDL of a table outside the filter without SHOW_DDL: silent", count(plain.commit(A, 600), "60011") == 0);
    }
    {
        Env env;
        Record rb(0x0506, Xid(5, 1, 0), FILTERED);
        rb.r.opc = 0x0B01;
        rb.r.flg = OpCode::FLG_USERUNDODDONE;
        rb.r.usn = 5;
        rb.r.slt = 1;
        const std::string err = captureStderr([&] { ParserTestAccess::rollback(*env.parser, &rb.r); });
        check("partial rollback of an unknown xid: 60010 as before, not 60011", count(err, "60010") == 1 && count(err, "60011") == 0);
    }
    {
        Env env(static_cast<uint>(Ctx::REDO_FLAGS::SHOW_INCOMPLETE_TRANSACTIONS));
        env.dml(A, 0x0B02, FILTERED);
        check("flag 1024: the transaction is created, nothing tracked here (no double warning)",
              env.buffer->unknownXids.empty() && env.buffer->findTransaction(nullptr, A, 0, 0, true, false, false) != nullptr);
    }
    {
        Env env;
        Transaction* t = env.buffer->findTransaction(nullptr, A, 0, 0, false, true, false);
        t->begin = true;
        env.dml(A, 0x0B02, FILTERED);
        check("known transaction: nothing tracked", env.buffer->unknownXids.empty() && t->size > 0);
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
