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
#include "../parser/Transaction.h"
#include "../parser/TransactionBuffer.h"

// The parser's checkpoint step and the restart are driven through TransactionBuffer, Transaction, Metadata and
// SerializerJson the way Parser and Replicator call them; redo records are empty stubs with only the op codes set.
using namespace OpenLogReplicator;

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

    RedoLogRecord record(uint16_t opCode, typeObj obj = 0) {
        RedoLogRecord r{};
        r.clear();
        r.opCode = opCode;
        r.obj = obj;
        return r;
    }

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
            RedoLogRecord r1 = record(0x0501);
            const RedoLogRecord r2 = record(0x0513);
            t->attributes.insert_or_assign(Attribute::KEY::LOGIN_USER_NAME, user);
            t->add(metadata.get(), buffer.get(), &r1, &r2);
        }

        // 5.1 + 11.2: an insert into a table in the filter
        void insert(Transaction* t, typeObj obj) {
            RedoLogRecord r1 = record(0x0501, obj);
            RedoLogRecord r2 = record(0x0B02, obj);
            t->add(metadata.get(), buffer.get(), &r1, &r2);
        }

        DormantCheckpoint checkpoint(uint64_t scn) {
            Seq minSequence = Seq::none();
            FileOffset minFileOffset;
            Xid minXid;
            buffer->checkpoint(Scn(scn), minSequence, minFileOffset, minXid);
            DormantCheckpoint dormant;
            buffer->checkpointDormant(ctx.dormantTransactionsMax, dormant);
            return dormant;
        }

        // As Parser: metadata->checkpoint at the end of an LWN, then the checkpoint file
        std::string writeCheckpoint(uint64_t scn, uint32_t seq, uint64_t offset, bool withDormant = true) {
            Seq minSequence = Seq::none();
            FileOffset minFileOffset;
            Xid minXid;
            buffer->checkpoint(Scn(scn), minSequence, minFileOffset, minXid);
            DormantCheckpoint dormant;
            if (ctx.dormantTransactionsMax > 0)
                buffer->checkpointDormant(ctx.dormantTransactionsMax, dormant);
            metadata->checkpoint(&thread, Scn(scn), Time(0), Seq(seq), FileOffset(offset), 0, minSequence, minFileOffset, minXid,
                                 withDormant && ctx.dormantTransactionsMax > 0 ? &dormant : nullptr);
            std::ostringstream ss;
            SerializerJson serializer;
            serializer.serialize(metadata.get(), ss, false);
            return ss.str();
        }
    };

    struct Restart {
        bool ok{false};
        Seq sequence{Seq::none()};
        FileOffset fileOffset;
        std::vector<DormantTransaction> seed;
    };

    // As Metadata::readCheckpoint on a fresh process
    Restart readCheckpoint(const std::string& json, uint64_t dormantMax, const std::string& fingerprint) {
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
        restart.sequence = metadata.sequence;
        restart.fileOffset = metadata.fileOffset;
        restart.seed = metadata.dormantSeed;
        return restart;
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
        check("only dormant: no position, the checkpoint position is used", c.valid && c.sequence == Seq::none());
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
        RedoLogRecord lob = record(0x1A06);
        lob.sequence = Seq(11);
        lob.fileOffset = FileOffset(4096 + 16);
        env.buffer->addOrphanedLob(&lob);
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
        RedoLogRecord u1 = record(0x0B03, 77);
        const RedoLogRecord u2 = record(0x0501, 77);
        d1->rollbackLastOp(env.metadata.get(), env.buffer.get(), &u1, &u2);
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
