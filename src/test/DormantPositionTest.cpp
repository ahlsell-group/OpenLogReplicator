/* Test: the stream position after a restart past dormant transactions equals the one of a run without it
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
#include "../common/Attribute.h"
#include "../common/Ctx.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/DormantTransaction.h"
#include "../common/Format.h"
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

// D holds only session records and commits at 1000 without rows; T2 commits after it in redo at 990. Without a restart
// D's commit moves the stream position to 1000 (processBegin; processCommit skips the empty message) and T2 is sent at
// 1000. A client which confirmed (1000, i) in T2 restarts with START 1000. Read from min-tran, D's begin and session
// records are read again and T2 is sent at 1000 again. Recreated from the dormant state, D has to do the same, or T2,
// whose commit SCN is below START, is skipped.
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

    Format testFormat() {
        return {Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT};
    }

    const Xid D(1, 1, 101);
    const Xid T2(2, 2, 202);
    const Xid A(3, 3, 303);
    constexpr typeObj FILTERED = 77;

    struct Env {
        Ctx ctx;
        TestThread thread{&ctx};
        Locales locales;
        std::unique_ptr<Metadata> metadata;
        std::unique_ptr<TransactionBuffer> buffer;
        Format format{testFormat()};
        std::unique_ptr<BuilderJson> builder;
        std::unique_ptr<Parser> parser;

        explicit Env(uint64_t firstDataScn) {
            ctx.parserThread = &thread;
            ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
            metadata = std::make_unique<Metadata>(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
            metadata->firstDataScn = Scn(firstDataScn);
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
        }

        // As Parser::appendToTransactionBegin
        Transaction* begin(Xid xid, uint64_t offset, uint64_t scn) {
            Transaction* t = buffer->findTransaction(nullptr, xid, 0, 1, false, true, false);
            t->begin = true;
            t->beginSequence = Seq(10);
            t->beginScn = Scn(scn);
            t->beginFileOffset = FileOffset(offset);
            t->attributes.insert_or_assign(Attribute::KEY::LOGIN_USER_NAME, "U");
            return t;
        }

        // 5.1 + 5.19 through the parser
        void session(Xid xid) {
            Record r1(0x0501, xid), r2(0x0513, xid);
            ParserTestAccess::dml(*parser, &r1.r, &r2.r);
        }

        Transaction* recreate(Xid xid, uint64_t offset, uint64_t scn, bool buffered) {
            DormantTransaction d;
            d.xid = xid;
            d.thread = 1;
            d.beginSequence = Seq(10);
            d.beginFileOffset = FileOffset(offset);
            d.beginScn = Scn(scn);
            auto attributes = std::make_shared<AttributeMap>();
            attributes->insert_or_assign(Attribute::KEY::LOGIN_USER_NAME, "U");
            d.attributes = attributes;
            d.buffered = buffered;
            return buffer->seedDormant(nullptr, d);
        }

        std::string commit(Xid xid, uint64_t scn, bool rollback = false) {
            Record c(0x0504, xid, 0, scn);
            if (rollback)
                c.r.flg = OpCode::FLG_ROLLBACK_OP0504;
            return captureStderr([&] { ParserTestAccess::commit(*parser, &c.r); });
        }
    };

    struct Position {
        Scn lwnScn{Scn::none()};
        typeIdx lwnIdx{0};
        // Commits sent below the position: here only T2's (sent at all, at a position above its commit SCN)
        uint64_t below{0};

        bool operator==(const Position& other) const {
            return lwnScn == other.lwnScn && lwnIdx == other.lwnIdx && below == other.below;
        }
    };

    Position position(const Env& env) {
        return {env.builder->lwnScn, env.builder->lwnIdx, env.builder->commitsBelowPosition};
    }

    // D (begun at offset 1024, scn 100), then T2 committing below D. sessionBefore: D's session records lie before the
    // dormant position (read only by a run that reads D's begin). Returns the position after T2's commit.
    enum class Start { ORIGINAL, MIN_TRAN, DORMANT };

    Position run(Start start, bool sessionBefore, bool dRollback = false) {
        Env env(start == Start::ORIGINAL ? 500 : 1000);
        if (start == Start::DORMANT)
            static_cast<void>(env.recreate(D, 1024, 100, sessionBefore));
        else {
            static_cast<void>(env.begin(D, 1024, 100));
            if (sessionBefore)
                env.session(D);
        }
        static_cast<void>(env.commit(D, 1000, dRollback));
        static_cast<void>(env.begin(T2, 4096, 980));
        env.session(T2);
        static_cast<void>(env.commit(T2, 990));
        return position(env);
    }
}

int main() {
    {
        const Position original = run(Start::ORIGINAL, true);
        check("original: D moves the position to 1000, T2 is sent there", original.lwnScn == Scn(1000) && original.below == 1);
        const Position minTran = run(Start::MIN_TRAN, true);
        check("min-tran restart at 1000: T2 sent at 1000 as in the original", minTran == original);
        const Position dormant = run(Start::DORMANT, true);
        check("dormant restart at 1000, D had session records: T2 sent at 1000", dormant.lwnScn == Scn(1000) && dormant.below == 1);
                check("dormant restart: position identical to the min-tran restart", dormant == minTran);
    }
    {
        // D never buffered anything: its commit takes no position in any run, T2 at 990 is below START 1000 in all restarts
        const Position original = run(Start::ORIGINAL, false);
        check("empty D, original: T2 sent at its own commit scn", original.lwnScn == Scn(990) && original.below == 0);
        const Position minTran = run(Start::MIN_TRAN, false);
        const Position dormant = run(Start::DORMANT, false);
        check("empty D, min-tran restart at 1000: T2 not sent", minTran.lwnScn == Scn::none() && minTran.below == 0);
        check("empty D: dormant restart identical to the min-tran restart", dormant == minTran);
    }
    {
        // D rolled back: no position in any run
        const Position minTran = run(Start::MIN_TRAN, true, true);
        const Position dormant = run(Start::DORMANT, true, true);
        check("rolled back D: no position, T2 not sent", minTran.lwnScn == Scn::none() && minTran.below == 0);
        check("rolled back D: dormant restart identical to the min-tran restart", dormant == minTran);
    }
    {
        // D buffers rows after the dormant position and rolls them back to savepoint: still the original's position
        Env minTran(1000);
        static_cast<void>(minTran.begin(D, 1024, 100));
        minTran.session(D);
        Env dormant(1000);
        static_cast<void>(dormant.recreate(D, 1024, 100, true));
        for (Env* env: {&minTran, &dormant}) {
            Transaction* t = env->buffer->findTransaction(nullptr, D, 0, 1, true, false, false);
            Record i1(0x0501, D, FILTERED), i2(0x0B02, D, FILTERED);
            t->add(env->metadata.get(), env->buffer.get(), &i1.r, &i2.r);
            Record u1(0x0B03, D, FILTERED), u2(0x0501, D, FILTERED);
            t->rollbackLastOp(env->metadata.get(), env->buffer.get(), &u1.r, &u2.r);
            static_cast<void>(env->commit(D, 1000));
            static_cast<void>(env->begin(T2, 4096, 980));
            env->session(T2);
            static_cast<void>(env->commit(T2, 990));
        }
        check("partial rollback in D after the restart: identical to the min-tran restart", position(dormant) == position(minTran) &&
              position(dormant).lwnScn == Scn(1000) && position(dormant).below == 1);
    }
    {
        // Recreated again: a transaction recreated with buffered keeps it in the next dormant state (opCodes is 0 then)
        Env env(1000);
        static_cast<void>(env.recreate(D, 1024, 100, true));
        static_cast<void>(env.recreate(T2, 2048, 110, false));
        Seq ms = Seq::none();
        FileOffset mo;
        Xid mx;
        env.buffer->checkpoint(Scn(1100), ms, mo, mx);
        DormantCheckpoint c;
        env.buffer->checkpointDormant(1000, Seq(12), FileOffset(8192), c);
        check("computed again: buffered kept for the recreated one, not set for the empty one", c.transactions.size() == 2 &&
              c.transactions[0].xid == D && c.transactions[0].buffered && c.transactions[1].xid == T2 && !c.transactions[1].buffered);
    }
    {
        // A cold start retried after a failed one drops the dormant restart: a named transaction lost then is 60011, not 60061
        Env env(500);
        env.metadata->dormantRestart = true;
        env.metadata->dormantKnownXids.insert(A);
        env.metadata->dormantSeed.push_back(DormantTransaction{});
        auto* table = new DbTable(FILTERED, FILTERED, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "T");
        table->addColumn(new DbColumn(1, -1, 1, "ID", SysCol::COLTYPE::VARCHAR, 10, -1, -1, 0, 1, false, false, false, false, false, false,
                                      false, false, false));
        env.metadata->schema->tablePartitionMap.insert_or_assign(FILTERED, table);
        Record r1(0x0501, A, FILTERED), r2(0x0B02, A, FILTERED);
        ParserTestAccess::dml(*env.parser, &r1.r, &r2.r);
        env.metadata->dropDormantRestart();
        check("retried cold start: dormant state cleared", !env.metadata->dormantRestart && env.metadata->dormantKnownXids.empty() &&
              env.metadata->dormantSeed.empty());
        const std::string err = env.commit(A, 600);
        check("retried cold start: lost transaction is 60011, not 60061", contains(err, "60011") && !contains(err, "60061"));
        env.metadata->schema->tablePartitionMap.clear();
        delete table;
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
