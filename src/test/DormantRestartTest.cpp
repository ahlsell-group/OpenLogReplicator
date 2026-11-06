/* Test for restarting past dormant transactions (state.dormant-transactions-max)
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
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <rapidjson/document.h>

#include "../common/Attribute.h"
#include "../common/Ctx.h"
#include "../common/DormantTransaction.h"
#include "../common/RedoLogRecord.h"
#include "../common/Thread.h"
#include "../common/exception/DataException.h"
#include "../common/exception/RedoLogException.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../metadata/SerializerJson.h"
#include "../metadata/Schema.h"
#include "../common/DbColumn.h"
#include "../common/DbTable.h"
#include "../common/table/SysCol.h"
#include "../parser/OpCode.h"
#include "../parser/Parser.h"
#include "../parser/Transaction.h"
#include "../parser/TransactionBuffer.h"

// The parser's checkpoint step and the restart are driven through TransactionBuffer, Transaction, Metadata and
// SerializerJson the way Parser and Replicator call them; redo records are empty stubs with only the op codes set.
using namespace OpenLogReplicator;

namespace OpenLogReplicator {
    struct ParserTestAccess {
        static void append(Parser& parser, RedoLogRecord* r1, RedoLogRecord* r2) { parser.appendToTransaction(r1, r2); }
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

        explicit Record(uint16_t opCode, typeObj obj = 0) {
            r.clear();
            r.opCode = opCode;
            r.obj = obj;
        }
    };

    struct Env {
        Ctx ctx;
        TestThread thread{&ctx};
        Locales locales;
        std::unique_ptr<Metadata> metadata;
        std::unique_ptr<TransactionBuffer> buffer;

        explicit Env(uint64_t dormantMax) {
            ctx.parserThread = &thread;
            ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
            ctx.dormantTransactionsMax = dormantMax;
            metadata = std::make_unique<Metadata>(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
            metadata->nlsCharacterSet = "AL32UTF8";
            metadata->nlsNcharCharacterSet = "AL16UTF16";
            metadata->dbTimezoneStr = "+00:00";
            metadata->dormantFingerprint = "fp-1";
            buffer = std::make_unique<TransactionBuffer>(&ctx);
        }

        // Without a memory manager thread the released swap chunks stay; free them here
        ~Env() {
            buffer->purge();
            for (const auto& [_, sc]: ctx.swapChunks)
                delete sc;
            ctx.swapChunks.clear();
        }

        // As Parser::appendToTransactionBegin
        Transaction* begin(Xid xid, uint32_t seq, uint64_t offset, uint64_t scn, typeConId conId = 0) {
            Transaction* t = buffer->findTransaction(nullptr, xid, conId, 1, false, true, false);
            t->begin = true;
            t->beginSequence = Seq(seq);
            t->beginScn = Scn(scn);
            t->beginTimestamp = Time(1000 + static_cast<uint32_t>(scn));
            t->beginFileOffset = FileOffset(offset);
            return t;
        }

        // 5.1 + 5.19: session information, buffered for every transaction
        void session(Transaction* t, const std::string& user) {
            Record r1(0x0501);
            Record r2(0x0513);
            t->attributes.insert_or_assign(Attribute::KEY::LOGIN_USER_NAME, user);
            t->add(metadata.get(), buffer.get(), &r1.r, &r2.r);
        }

        // 5.1 + 11.2: an insert into a table in the filter
        void insert(Transaction* t, typeObj obj) {
            Record r1(0x0501, obj);
            Record r2(0x0B02, obj);
            t->add(metadata.get(), buffer.get(), &r1.r, &r2.r);
        }

        DormantCheckpoint checkpoint(uint64_t scn) {
            Seq minSequence = Seq::none();
            FileOffset minFileOffset;
            Xid minXid;
            buffer->checkpoint(Scn(scn), minSequence, minFileOffset, minXid);
            DormantCheckpoint dormant;
            buffer->checkpointDormant(ctx.dormantTransactionsMax, Seq(12), FileOffset(8192), dormant);
            return dormant;
        }

        // As Parser: metadata->checkpoint at the end of an LWN, then the checkpoint file
        // computeDormant = false: as Parser within the same second of redo, the last computed state is kept
        std::string writeCheckpoint(uint64_t scn, uint32_t seq, uint64_t offset, bool computeDormant = true) {
            Seq minSequence = Seq::none();
            FileOffset minFileOffset;
            Xid minXid;
            buffer->checkpoint(Scn(scn), minSequence, minFileOffset, minXid);
            DormantCheckpoint dormant;
            if (computeDormant && ctx.dormantTransactionsMax > 0)
                buffer->checkpointDormant(ctx.dormantTransactionsMax, Seq(seq), FileOffset(offset), dormant);
            metadata->checkpoint(&thread, Scn(scn), Time(0), Seq(seq), FileOffset(offset), 0, minSequence, minFileOffset, minXid,
                                 computeDormant && ctx.dormantTransactionsMax > 0 ? &dormant : nullptr);
            std::ostringstream ss;
            SerializerJson serializer;
            serializer.serialize(metadata.get(), ss, false);
            return ss.str();
        }
    };

    struct Restart {
        bool ok{false};
        bool engaged{false};
        std::set<Xid> known;
        Seq sequence{Seq::none()};
        FileOffset fileOffset;
        std::vector<DormantTransaction> seed;
    };

    // As Metadata::readCheckpoint on a fresh process
    Restart readCheckpoint(const std::string& json, uint64_t dormantMax, const std::string& fingerprint, bool schemaLoaded = true) {
        Ctx ctx;
        TestThread thread(&ctx);
        ctx.parserThread = &thread;
        ctx.dormantTransactionsMax = dormantMax;
        Locales locales;
        locales.initialize();
        Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
        metadata.dormantFingerprint = fingerprint;
        SerializerJson serializer;
        std::vector<std::string> msgs;
        std::unordered_map<typeObj, std::string> tablesUpdated;
        Restart restart;
        restart.ok = serializer.deserialize(&metadata, json, "TEST-chkpt-1", msgs, tablesUpdated, true, false);
        // as Metadata::readCheckpoint after the schema is loaded
        if (restart.ok && schemaLoaded) {
            metadata.schema->scn = Scn(1);
            metadata.applyDormantRestart("TEST-chkpt-1");
        } else
            metadata.dropDormantPending("TEST-chkpt-1");
        restart.engaged = metadata.dormantRestart;
        restart.known = metadata.dormantKnownXids;
        restart.sequence = metadata.sequence;
        restart.fileOffset = metadata.fileOffset;
        restart.seed = metadata.dormantSeed;
        return restart;
    }

    // stderr (warnings) of fn
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

    bool hasXid(const std::vector<DormantTransaction>& list, Xid xid) {
        for (const auto& d: list)
            if (d.xid == xid)
                return true;
        return false;
    }

    const Xid D1(1, 1, 101);
    const Xid D2(2, 2, 202);
    const Xid R1(3, 3, 303);
    const Xid R2(4, 4, 404);
}

int main() {
    {
        Env env(0);
        Transaction* d = env.begin(D1, 10, 1024, 100);
        env.session(d, "U1");
        const std::string json = env.writeCheckpoint(500, 12, 8192);
        check("off: checkpoint has min-tran of the dormant transaction", contains(json, R"("min-tran":{"seq":10,"offset":1024)"));
        check("off: checkpoint has no dormant-tran", !contains(json, "dormant-tran"));

        Env onNoField(1000);
        // nullptr dormant (the parser when off) writes exactly what an older version writes
        Transaction* d3 = onNoField.begin(D1, 10, 1024, 100);
        onNoField.session(d3, "U1");
        check("off: output equals a checkpoint without dormant state", json == onNoField.writeCheckpoint(500, 12, 8192, false));

        // A relevant transaction older than every dormant one: the position is min-tran, nothing to write
        Env older(1000);
        Transaction* r = older.begin(R1, 9, 512, 90);
        older.insert(r, 77);
        Transaction* d4 = older.begin(D1, 10, 1024, 100);
        older.session(d4, "U1");
        check("not later than min-tran: no dormant-tran", !contains(older.writeCheckpoint(500, 12, 8192), "dormant-tran"));
    }

    {
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        Transaction* r1 = env.begin(R1, 11, 2048, 200);
        env.insert(r1, 77);
        Transaction* s = env.begin(Xid(5, 5, 505), 9, 512, 90);
        s->system = true;
        Transaction* l = env.begin(Xid(6, 6, 606), 9, 1024, 91);
        l->lobCtx.listMap.insert_or_assign(1, new uint8_t[1]);
        Transaction* sp = env.begin(Xid(7, 7, 707), 9, 1536, 92);
        sp->lastSplit = true;
        Transaction* noBegin = env.buffer->findTransaction(nullptr, Xid(8, 8, 808), 0, 1, false, true, false);
        static_cast<void>(noBegin);
        Transaction* e = env.begin(D2, 11, 4096, 210);

        const DormantCheckpoint c = env.checkpoint(500);
        check("classify: session-only and empty transactions are dormant",
              c.transactions.size() == 2 && hasXid(c.transactions, D1) && hasXid(c.transactions, D2));
        check("classify: system, LOB state, split record and no-begin transactions pin the position",
              c.sequence == Seq(9) && c.fileOffset == FileOffset(512) && c.xid == Xid(5, 5, 505));
        check("classify: dormant list is ordered by begin", c.transactions[0].xid == D1 && c.transactions[1].xid == D2);
        check("classify: attributes are kept", c.transactions[0].attributes != nullptr &&
              c.transactions[0].attributes->at(Attribute::KEY::LOGIN_USER_NAME) == "U1");
        static_cast<void>(e);
    }

    {
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        const DormantCheckpoint c = env.checkpoint(500);
        check("only dormant: the position is the checkpoint", c.valid && c.sequence == Seq(12) && c.fileOffset == FileOffset(8192) &&
              c.xid == Xid::zero());
        const std::string json = env.writeCheckpoint(500, 12, 8192);
        check("only dormant: dormant-tran points at the checkpoint", contains(json, R"("dormant-tran":{"fingerprint":"fp-1","seq":12,"offset":8192,"list")"));
        check("only dormant: min-tran stays the classic one", contains(json, R"("min-tran":{"seq":10,"offset":1024)"));

        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        check("restart: starts at the checkpoint", restart.ok && restart.sequence == Seq(12) && restart.fileOffset == FileOffset(8192));
        check("restart: recreates the dormant transaction", restart.seed.size() == 1 && restart.seed[0].xid == D1 &&
              restart.seed[0].beginSequence == Seq(10) && restart.seed[0].beginFileOffset == FileOffset(1024) &&
              restart.seed[0].beginScn == Scn(100) && restart.seed[0].beginTimestamp.getVal() == 1100 && restart.seed[0].thread == 1);
        check("restart: with its attributes", restart.seed.size() == 1 && restart.seed[0].attributes != nullptr &&
              restart.seed[0].attributes->at(Attribute::KEY::LOGIN_USER_NAME) == "U1");

        const Restart off = readCheckpoint(json, 0, "fp-1");
        check("toggled off: restart from min-tran, nothing recreated", off.ok && off.sequence == Seq(10) &&
              off.fileOffset == FileOffset(1024) && off.seed.empty());
        const Restart changed = readCheckpoint(json, 1000, "fp-2");
        check("filter or flags changed: restart from min-tran, nothing recreated", changed.ok && changed.sequence == Seq(10) &&
              changed.seed.empty());

        // ahlsell.13 and older check the top-level names; dormant-tran is not among them
        rapidjson::Document document;
        document.Parse(json.c_str());
        static const std::vector<std::string> oldNames{
            "activation", "big-endian", "con-id", "con-name", "context", "database", "db-block-checksum", "db-id", "db-recovery-file-dest",
            "db-timezone", "incarnations", "log-archive-dest", "log-archive-format", "min-tran", "nls-character-set",
            "nls-nchar-character-set", "offset", "online-redo", "resetlogs", "schema-ref-scn", "schema-scn", "scn", "seq",
            "supp-log-db-all", "supp-log-db-primary", "sys-ccol", "sys-cdef", "sys-col", "sys-deferredstg", "sys-ecol", "sys-lob",
            "sys-lob-comp-part", "sys-lob-frag", "sys-obj", "sys-tab", "sys-tabcompart", "sys-tabpart", "sys-tabsubpart", "sys-ts",
            "sys-user", "time", "users", "xdb-ttset"};
        bool rejected = false;
        try {
            Ctx::checkJsonFields("TEST-chkpt-1", document, oldNames);
        } catch (const DataException& ex) {
            rejected = ex.code == 20003;
        }
        check("older version: rejects the checkpoint (error 20003), takes an older one", rejected);
    }

    {
        // A checkpoint written before the feature: no dormant-tran
        Env env(0);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        const std::string json = env.writeCheckpoint(500, 12, 8192);
        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        check("toggled on, old checkpoint: restart from min-tran", restart.ok && restart.sequence == Seq(10) && restart.seed.empty());
    }

    {
        // A transaction that is not dormant pins the position between the begins of two dormant ones
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        Transaction* r1 = env.begin(R1, 11, 2048, 200);
        env.insert(r1, 77);
        Transaction* d2 = env.begin(D2, 11, 4096, 210);
        env.session(d2, "U2");
        Transaction* d3 = env.begin(Xid(9, 9, 909), 11, 2048, 205);
        env.session(d3, "U3");
        const std::string json = env.writeCheckpoint(500, 12, 8192);
        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        check("pinned: restart at the begin of the relevant transaction", restart.sequence == Seq(11) && restart.fileOffset == FileOffset(2048));
        check("pinned: dormant begun before it is recreated", hasXid(restart.seed, D1));
        check("pinned: dormant begun after it is read again, not recreated", !hasXid(restart.seed, D2));
        check("pinned: dormant begun in the same LWN is read again, not recreated", !hasXid(restart.seed, Xid(9, 9, 909)));
        check("pinned: one recreated", restart.seed.size() == 1);
        check("pinned: the checkpoint names the pinned and all dormant transactions", contains(json, "\"pinned\":[\"" + R1.toString() + "\"]") &&
              restart.known.size() == 4 && restart.known.count(R1) == 1 && restart.known.count(D2) == 1);
    }

    {
        // State computed at an earlier LWN, checkpoint written later: a transaction begun in between reads again
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        static_cast<void>(env.writeCheckpoint(500, 12, 8192));
        Transaction* r1 = env.begin(R1, 12, 9216, 520);
        env.insert(r1, 77);
        env.insert(d1, 77);
        const std::string json = env.writeCheckpoint(530, 13, 4096, false);
        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        check("stale state: restart at the position it was computed at", restart.sequence == Seq(12) && restart.fileOffset == FileOffset(8192));
        check("stale state: rows buffered since are read again", restart.seed.size() == 1 && restart.seed[0].xid == D1);
        check("stale state: min-tran of the later checkpoint unchanged", contains(json, R"("min-tran":{"seq":10,"offset":1024)"));
    }

    {
        // Committed at or after the LWN of the checkpoint: pins as before
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        env.buffer->addCommitted(Scn(510), Scn(0), Seq(11), FileOffset(512), R2);
        const DormantCheckpoint c = env.checkpoint(505);
        check("committed in the checkpoint LWN pins the position", c.sequence == Seq(11) && c.fileOffset == FileOffset(512) && c.xid == R2);
        check("committed in the checkpoint LWN: dormant still listed", c.transactions.size() == 1);
    }

    {
        // A LOB page without a transaction may belong to any transaction open when it was written
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        Transaction* d2 = env.begin(D2, 11, 8192, 210);
        env.session(d2, "U2");
        Record lob(0x1A06);
        lob.r.sequence = Seq(11);
        lob.r.fileOffset = FileOffset(4096 + 16);
        env.buffer->addOrphanedLob(&lob.r);
        const DormantCheckpoint c = env.checkpoint(500);
        check("orphaned LOB: dormant begun before it pins the position", c.sequence == Seq(10) && c.fileOffset == FileOffset(1024) && c.xid == D1);
        check("orphaned LOB: dormant begun after it stays dormant", c.transactions.size() == 1 && c.transactions[0].xid == D2);
    }

    {
        // Bound on the checkpoint size
        Env env(2);
        for (uint32_t i = 0; i < 5; ++i) {
            Transaction* t = env.begin(Xid(static_cast<typeUsn>(20 + i), 1, 1), 10 + i, 1024, 100 + i);
            env.session(t, "U");
        }
        const DormantCheckpoint c = env.checkpoint(500);
        check("max: keeps the oldest", c.transactions.size() == 2 && c.transactions[0].xid == Xid(20, 1, 1) && c.transactions[1].xid == Xid(21, 1, 1));
        check("max: the rest pin the position at the oldest of them", c.sequence == Seq(12) && c.xid == Xid(22, 1, 1));
    }

    {
        // Dormant at checkpoint N, relevant after it: restart from N recreates it and reads its rows again
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        const std::string json = env.writeCheckpoint(500, 12, 8192);
        env.insert(d1, 77);
        const DormantCheckpoint later = env.checkpoint(600);
        check("becomes relevant: the next checkpoint pins it", later.sequence == Seq(10) && later.transactions.empty());

        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        Env next(1000);
        for (const DormantTransaction& d: restart.seed)
            static_cast<void>(next.buffer->seedDormant(nullptr, d));
        Transaction* seeded = next.buffer->findTransaction(nullptr, D1, 0, 1, true, false, false);
        check("recreated: found with its begin", seeded != nullptr && seeded->begin && seeded->beginScn == Scn(100) &&
              seeded->beginSequence == Seq(10) && seeded->beginFileOffset == FileOffset(1024));
        check("recreated: attributes restored", seeded != nullptr && seeded->attributes.at(Attribute::KEY::LOGIN_USER_NAME) == "U1");
        check("recreated: not relevant until it buffers rows", seeded != nullptr && !seeded->holdsRestartState());
        if (seeded != nullptr) {
            next.insert(seeded, 77);
            check("recreated: buffers rows after the restart", seeded->holdsRestartState() && seeded->size > 0);
        }

        bool conflict = false;
        try {
            static_cast<void>(next.buffer->findTransaction(nullptr, D1, 0, 1, false, true, false));
        } catch (const RedoLogException& ex) {
            conflict = ex.code == 50039;
        }
        check("recreated: its begin read again would conflict (so it is never recreated then)", conflict);

        // Commit, then the slot is reused with another sequence number
        next.buffer->dropTransaction(D1, 0);
        if (seeded != nullptr) {
            seeded->purge(&next.ctx);
            delete seeded;
        }
        Transaction* reuse = next.buffer->findTransaction(nullptr, Xid(1, 1, 102), 0, 1, false, true, false);
        check("xid reuse after the commit of a recreated transaction", reuse != nullptr && reuse->xid == Xid(1, 1, 102));
    }

    {
        // Rows rolled back to a savepoint: stays pinned (conservative)
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        env.insert(d1, 77);
        Record u1(0x0B03, 77);
        Record u2(0x0501, 77);
        d1->rollbackLastOp(env.metadata.get(), env.buffer.get(), &u1.r, &u2.r);
        const DormantCheckpoint c = env.checkpoint(500);
        check("partial rollback of all rows: still pins", c.sequence == Seq(10) && c.transactions.empty());
    }

    {
        // Container id and a slot above 0xFFF round-trip
        Env env(1000);
        const Xid wide(0x0134, 0x100b, 0x00450290);
        Transaction* d1 = env.begin(wide, 10, 1024, 100, 3);
        env.session(d1, "U\"1");
        const std::string json = env.writeCheckpoint(500, 12, 8192);
        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        check("round-trip: xid, con-id and escaped attribute", restart.seed.size() == 1 && restart.seed[0].xid == wide &&
              restart.seed[0].conId == 3 && restart.seed[0].attributes->at(Attribute::KEY::LOGIN_USER_NAME) == "U\"1");

        Env next(1000);
        Transaction* t = restart.seed.empty() ? nullptr : next.buffer->seedDormant(nullptr, restart.seed[0]);
        check("round-trip: recreated under the same container", next.buffer->findTransaction(nullptr, wide, 3, 1, true, false, false) == t &&
              next.buffer->findTransaction(nullptr, wide, 0, 1, true, false, false) == nullptr);
    }

    {
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        std::string json = env.writeCheckpoint(500, 12, 8192);
        // dormant position before min-tran: inconsistent
        std::string bad = json;
        const std::string from = R"("dormant-tran":{"fingerprint":"fp-1","seq":12)";
        bad.replace(bad.find(from), from.size(), R"("dormant-tran":{"fingerprint":"fp-1","seq":9)");
        const Restart restart = readCheckpoint(bad, 1000, "fp-1");
        check("inconsistent position: restart from min-tran", restart.ok && restart.sequence == Seq(10) && restart.seed.empty());

        std::string unknown = json;
        unknown.replace(unknown.find("login username"), 14, "no such name");
        check("unknown attribute: checkpoint rejected", !readCheckpoint(unknown, 1000, "fp-1").ok);
    }

    {
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        static_cast<void>(env.checkpoint(500));
        const auto first = d1->attributesSnapshot;
        static_cast<void>(env.checkpoint(501));
        check("attributes: snapshot reused while unchanged", d1->attributesSnapshot == first);
        d1->attributes.insert_or_assign(Attribute::KEY::OS_USER_NAME, "os");
        static_cast<void>(env.checkpoint(502));
        check("attributes: new snapshot after a change", d1->attributesSnapshot != first && first->size() == 1 &&
              d1->attributesSnapshot->size() == 2);
    }

    {
        // Parser classification: session information alone, a row of a table outside the filter, a row of a filtered table
        Env env(1000);
        auto* table = new DbTable(77, 77, 100, 0, DbTable::OPTIONS::DEFAULT, "TEST", "T");
        table->addColumn(new DbColumn(1, -1, 1, "ID", SysCol::COLTYPE::VARCHAR, 10, -1, -1, 0, 1, false, false, false, false, false, false,
                                      false, false, false));
        env.metadata->schema->tablePartitionMap.insert_or_assign(77, table);
        auto parserPtr = std::make_unique<Parser>(&env.ctx, nullptr, env.metadata.get(), env.buffer.get(), 0, "test");
        Parser& parser = *parserPtr;
        Transaction* s = env.begin(D1, 10, 1024, 100);
        Transaction* u = env.begin(D2, 10, 1536, 101);
        Transaction* f = env.begin(R1, 10, 2048, 102);
        Record s1(0x0501, 0), s2(0x0513, 0), u1(0x0501, 88), u2(0x0B05, 88), f1(0x0501, 77), f2(0x0B02, 77);
        for (auto* r: {&s1.r, &s2.r}) r->xid = D1;
        for (auto* r: {&u1.r, &u2.r}) r->xid = D2;
        for (auto* r: {&f1.r, &f2.r}) r->xid = R1;
        u1.r.dataObj = 88;
        f1.r.dataObj = 77;
        ParserTestAccess::append(parser, &s1.r, &s2.r);
        ParserTestAccess::append(parser, &u1.r, &u2.r);
        ParserTestAccess::append(parser, &f1.r, &f2.r);
        check("parser: [5.1, 5.19] buffered, not relevant", s->size > 0 && !s->holdsRestartState());
        check("parser: [5.1, 11.5] outside the filter not buffered, dormant", u->size == 0 && !u->holdsRestartState());
        check("parser: [5.1, 11.2] of a filtered table pins", f->size > 0 && f->holdsRestartState());
        const DormantCheckpoint c = env.checkpoint(500);
        check("parser: position at the filtered transaction, two dormant", c.sequence == Seq(10) && c.fileOffset == FileOffset(2048) &&
              c.transactions.size() == 2);

        // Safety net after a start past dormant transactions
        env.metadata->firstDataScn = Scn(600);
        Record commit(0x0504);
        commit.r.xid = Xid(40, 1, 1);
        commit.r.scn = Scn(700);
        std::string err = captureStderr([&] { ParserTestAccess::commit(parser, &commit.r); });
        check("net: silent without a dormant restart", !contains(err, "60061"));
        env.metadata->dormantRestart = true;
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit.r); });
        check("net: unknown commit the checkpoint did not name, nothing filtered: silent", !contains(err, "60061") && !contains(err, "60011"));
        env.metadata->dormantKnownXids.insert(commit.r.xid);
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit.r); });
        check("net: unknown commit of a transaction the checkpoint named warns 60061", contains(err, "60061") && !contains(err, "60011"));
        // A transaction OLR never knew (e.g. lost at an earlier cold start), with filtered rows after the restart
        Record r1(0x0501, 77), r2(0x0B02, 77);
        r1.r.xid = Xid(41, 1, 1);
        r2.r.xid = r1.r.xid;
        r1.r.dataObj = 77;
        ParserTestAccess::append(parser, &r1.r, &r2.r);
        Record commit2(0x0504);
        commit2.r.xid = r1.r.xid;
        commit2.r.scn = Scn(700);
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit2.r); });
        check("net: inherited unknown with filtered rows after a dormant restart: 60011, not 60061", contains(err, "60011") && !contains(err, "60061"));
        // Review probe: named lost at the cold start of this run, then a dormant restart... (lost list of this process)
        Record r3(0x0501, 77), r4(0x0B02, 77);
        r3.r.xid = Xid(42, 1, 1);
        r4.r.xid = r3.r.xid;
        r3.r.dataObj = 77;
        env.metadata->coldStartLostXids.insert(r3.r.xid);
        ParserTestAccess::append(parser, &r3.r, &r4.r);
        Record commit3(0x0504);
        commit3.r.xid = r3.r.xid;
        commit3.r.scn = Scn(700);
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit3.r); });
        check("net: named lost at the cold start, after a dormant restart: no 60061, no second 60011", !contains(err, "60061") && !contains(err, "60011"));
        // Review probe: unknown, only tables outside the filter
        Record r5(0x0501, 88), r6(0x0B05, 88);
        r5.r.xid = Xid(43, 1, 1);
        r6.r.xid = r5.r.xid;
        r5.r.dataObj = 88;
        ParserTestAccess::append(parser, &r5.r, &r6.r);
        Record commit4(0x0504);
        commit4.r.xid = r5.r.xid;
        commit4.r.scn = Scn(700);
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit4.r); });
        check("net: unknown outside the filter after a dormant restart: silent", !contains(err, "60061") && !contains(err, "60011"));
        commit.r.scn = Scn(599);
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit.r); });
        check("net: unknown commit below the first data scn is not reported", !contains(err, "60061"));
        commit.r.scn = Scn(700);
        commit.r.flg = OpCode::FLG_ROLLBACK_OP0504;
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit.r); });
        check("net: unknown rollback is not reported", !contains(err, "60061"));
        commit.r.flg = 0;
        env.metadata->conId = 3;
        commit.r.conId = 1;
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit.r); });
        check("net: commit of another container is not reported", !contains(err, "60061"));
        env.metadata->conId = 0;
        commit.r.conId = 0;
        env.buffer->skipXidList.insert(commit.r.xid);
        err = captureStderr([&] { ParserTestAccess::commit(parser, &commit.r); });
        check("net: skipped transaction is not reported", !contains(err, "60061"));
        env.metadata->schema->tablePartitionMap.clear();
        delete table;
    }

    {
        // Rollback record against a recreated transaction that never buffered anything
        Env env(1000);
        DormantTransaction d;
        d.xid = D1;
        d.thread = 1;
        d.beginSequence = Seq(10);
        d.beginFileOffset = FileOffset(1024);
        Transaction* t = env.buffer->seedDormant(nullptr, d);
        Record u(0x0B01, 77);
        std::string err = captureStderr([&] { t->rollbackLastOp(env.metadata.get(), env.buffer.get(), &u.r); });
        check("rollback: recreated and empty, no 70004", !contains(err, "70004"));
        Record u2(0x0B03, 77), u3(0x0501, 77);
        err = captureStderr([&] { t->rollbackLastOp(env.metadata.get(), env.buffer.get(), &u2.r, &u3.r); });
        check("rollback: recreated and empty, two vectors, no 70004", !contains(err, "70004"));
        Transaction* n = env.begin(D2, 10, 2048, 110);
        err = captureStderr([&] { n->rollbackLastOp(env.metadata.get(), env.buffer.get(), &u.r); });
        check("rollback: not recreated, 70004 as before", contains(err, "70004"));
    }

    {
        // Filter changed at runtime
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        DormantCheckpoint before;
        env.buffer->checkpointDormant(1000, Seq(12), FileOffset(8192), before);
        before.generation = env.metadata->dormantGeneration;
        env.metadata->changeDormantFingerprint(&env.thread, "");
        env.metadata->checkpoint(&env.thread, Scn(500), Time(0), Seq(12), FileOffset(8192), 0, Seq(10), FileOffset(1024), D1, &before);
        check("reload: state computed before the change is dropped", !env.metadata->dormant.valid);
        env.metadata->changeDormantFingerprint(&env.thread, "fp-2");
        env.buffer->pinOpenTransactions();
        const std::string json = env.writeCheckpoint(510, 12, 8704);
        check("reload: transactions open at the change pin", !contains(json, "dormant-tran"));
        Transaction* d2 = env.begin(D2, 12, 9216, 520);
        env.session(d2, "U2");
        env.buffer->dormantGeneration = env.metadata->dormantGeneration;
        Seq ms = Seq::none(); FileOffset mo; Xid mx;
        env.buffer->checkpoint(Scn(530), ms, mo, mx);
        DormantCheckpoint after;
        env.buffer->checkpointDormant(1000, Seq(13), FileOffset(512), after);
        after.generation = env.metadata->dormantGeneration;
        env.metadata->checkpoint(&env.thread, Scn(530), Time(0), Seq(13), FileOffset(512), 0, ms, mo, mx, &after);
        std::ostringstream ss;
        SerializerJson serializer;
        serializer.serialize(env.metadata.get(), ss, false);
        check("reload: a transaction open at the change pins, one begun after it is dormant",
              env.metadata->dormant.valid && env.metadata->dormant.sequence == Seq(10) && env.metadata->dormant.fileOffset == FileOffset(1024) &&
              env.metadata->dormant.transactions.size() == 1 && env.metadata->dormant.transactions[0].xid == D2);
        check("reload: so no dormant-tran (not later than min-tran)", !contains(ss.str(), "dormant-tran"));
        env.metadata->changeDormantFingerprint(&env.thread, "");
        std::ostringstream ss2;
        serializer.serialize(env.metadata.get(), ss2, false);
        check("reload: nothing written while the change is under way", !contains(ss2.str(), "dormant-tran"));
    }

    {
        // Schema of the checkpoint not loaded (referenced file missing, adaptive schema): min-tran, nothing recreated
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        const std::string json = env.writeCheckpoint(500, 12, 8192);
        const Restart restart = readCheckpoint(json, 1000, "fp-1", false);
        check("schema not loaded: min-tran", restart.ok && restart.sequence == Seq(10) && restart.seed.empty() && !restart.engaged);
        check("schema loaded: engaged", readCheckpoint(json, 1000, "fp-1").engaged);
    }

    {
        // Max 1 with the oldest transaction relevant: nothing later than min-tran, no field
        Env env(1);
        Transaction* r = env.begin(R1, 9, 512, 90);
        env.insert(r, 77);
        Transaction* a = env.begin(D1, 10, 1024, 100);
        env.session(a, "U1");
        Transaction* b = env.begin(D2, 11, 1024, 110);
        env.session(b, "U2");
        check("max 1, oldest relevant: no dormant-tran", !contains(env.writeCheckpoint(500, 12, 8192), "dormant-tran"));
    }

    {
        // Dormant at the computation LWN, committed in the checkpoint LWN at or above its scn, client starts at that commit scn
        Env env(1000);
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        static_cast<void>(env.writeCheckpoint(500, 12, 8192));
        env.insert(d1, 77);
        env.buffer->addCommitted(Scn(512), Scn(0), Seq(10), FileOffset(1024), D1);
        env.buffer->dropTransaction(D1, 0);
        d1->purge(&env.ctx);
        delete d1;
        const std::string json = env.writeCheckpoint(510, 12, 9216, false);
        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        check("commit in the checkpoint LWN: min-tran is its begin", contains(json, R"("min-tran":{"seq":10,"offset":1024)"));
        check("commit in the checkpoint LWN: start before its rows, recreated", restart.sequence == Seq(12) &&
              restart.fileOffset == FileOffset(8192) && hasXid(restart.seed, D1));
    }

    {
        // Slot reused between the computation LWN and the checkpoint; an older dormant transaction keeps min-tran before both
        Env env(1000);
        Transaction* d0 = env.begin(D2, 9, 512, 90);
        env.session(d0, "U0");
        Transaction* d1 = env.begin(D1, 10, 1024, 100);
        env.session(d1, "U1");
        static_cast<void>(env.writeCheckpoint(500, 12, 8192));
        env.buffer->dropTransaction(D1, 0);
        d1->purge(&env.ctx);
        delete d1;
        const Xid reuse(1, 1, 102);
        Transaction* e = env.begin(reuse, 12, 9216, 520);
        env.session(e, "U2");
        const std::string json = env.writeCheckpoint(530, 12, 9728, false);
        const Restart restart = readCheckpoint(json, 1000, "fp-1");
        check("slot reuse: started past both", restart.engaged && restart.seed.size() == 2);
        Env next(1000);
        for (const DormantTransaction& d: restart.seed)
            static_cast<void>(next.buffer->seedDormant(nullptr, d));
        // the commit of D1 is read again (after the start), then the begin of the reused slot
        Transaction* old = next.buffer->findTransaction(nullptr, D1, 0, 1, true, false, false);
        check("slot reuse: old transaction recreated", old != nullptr);
        next.buffer->dropTransaction(D1, 0);
        if (old != nullptr) {
            old->purge(&next.ctx);
            delete old;
        }
        bool ok = true;
        try {
            static_cast<void>(next.buffer->findTransaction(nullptr, reuse, 0, 1, false, true, false));
        } catch (const RedoLogException&) {
            ok = false;
        }
        check("slot reuse: new transaction begins after the commit", ok);
    }

    check("fingerprint: stable", dormantFingerprint("a") == dormantFingerprint("a") && dormantFingerprint("a").size() == 16);
    check("fingerprint: differs", dormantFingerprint(R"({"table":[{"owner":"A","table":"T"}]})") !=
          dormantFingerprint(R"({"table":[{"owner":"A","table":"T"},{"owner":"A","table":"U"}]})"));

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
