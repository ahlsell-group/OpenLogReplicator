/* Test for two dictionary rows sharing a unique key inside one transaction
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

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/exception/DataException.h"
#include "../common/table/SysObj.h"
#include "../common/table/TablePack.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../metadata/SerializerJson.h"
#include "../state/StateDisk.h"

// An online index rebuild swaps OBJ# between two SYS.OBJ$ rows with two updates in one transaction: after the first,
// both rows hold the same OBJ#. That stopped OLR with ERROR 50024 (VIVU, A_COINVROW_X4, 2026-11-25). The row taking
// the key now holds it with warning 60062 and the key goes back when the other row changes. A dictionary transaction
// that fails half applied must not be written to a checkpoint.
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

    using ObjPack = TablePack<SysObj, SysObjNameKey, SysObjObj>;

    constexpr typeObj OLD_OBJ = 37375559;
    constexpr typeObj NEW_OBJ = 37811249;

    SysObj* index(uint16_t slot, typeObj obj, typeDataObj dataObj, const std::string& name) {
        return new SysObj(RowId(1, 100, slot), 37, obj, dataObj, SysObj::OBJTYPE::INDEX, name, 0, 0, false);
    }

    // As SystemTransaction does: forUpdate drops the keys, the columns change, addKeys indexes the row again
    void updateObj(const Ctx& ctx, ObjPack& pack, SysObj* row, typeObj obj) {
        SysObj* const found = pack.forUpdate(&ctx, row->rowId, FileOffset::zero());
        found->obj = obj;
        pack.addKeys(&ctx, found);
    }

    SysObj* byObj(const ObjPack& pack, typeObj obj) {
        const auto& it = pack.unorderedMapKey.find(SysObjObj(obj));
        return it == pack.unorderedMapKey.end() ? nullptr : it->second;
    }

    struct Rows {
        SysObj* x4;
        SysObj* tmp;
    };

    // A_COINVROW_X4 (OLD_OBJ) and the rebuilt index I_SYS_..._A_COINVROW_X4 (NEW_OBJ), as before the swap
    Rows load(const Ctx& ctx, ObjPack& pack) {
        Rows rows{index(9, OLD_OBJ, OLD_OBJ, "A_COINVROW_X4"), index(3, NEW_OBJ, NEW_OBJ, "I_SYS_37375553_A_COINVROW_X4")};
        pack.addWithKeys(&ctx, rows.x4);
        pack.addWithKeys(&ctx, rows.tmp);
        return rows;
    }

    bool throwsMissing(const Ctx& ctx, ObjPack& pack, SysObj* row, typeObj obj) {
        try {
            updateObj(ctx, pack, row, obj);
        } catch (DataException& ex) {
            return ex.code == 50030;
        }
        return false;
    }

    // A checkpoint is written at each scn while the schema change counter has the given value
    bool writes(Ctx& ctx, Locales& locales, const std::string& dir, uint64_t scn, uint64_t schemaChanges) {
        Metadata metadata(&ctx, &locales, "TEST", Scn::none(), Seq::none(), "", 0);
        metadata.state = new StateDisk(metadata.ctx, dir);
        metadata.serializer = new SerializerJson();
        metadata.nlsCharacterSet = "AL32UTF8";
        metadata.nlsNcharCharacterSet = "AL16UTF16";
        metadata.dbTimezoneStr = "+00:00";
        metadata.setNlsCharset(metadata.nlsCharacterSet, metadata.nlsNcharCharacterSet);
        metadata.allowCheckpoints();
        metadata.schemaChanges = schemaChanges;
        metadata.sequence = Seq(static_cast<uint32_t>(scn / 10));
        metadata.checkpoint(ctx.parserThread, Scn(scn), Time(0), Seq(static_cast<uint32_t>(scn / 10)), FileOffset(512),
                            0, Seq::none(), FileOffset::zero(), Xid::zero(), Scn(scn));
        metadata.writeCheckpoint(ctx.parserThread, true);
        return std::filesystem::exists(dir + "/TEST-chkpt-" + std::to_string(scn) + ".json");
    }
}

int main() {
    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);

    {
        ObjPack pack;
        const Rows rows = load(ctx, pack);
        bool ok = true;
        try {
            updateObj(ctx, pack, rows.x4, NEW_OBJ);
        } catch (DataException&) {
            ok = false;
        }
        check("swap, index first: taking an OBJ# another row holds does not stop (was ERROR 50024)", ok);
        check("swap, index first: the row taking the key is found by it", byObj(pack, NEW_OBJ) == rows.x4);
        check("swap, index first: one key shared until the other row changes", pack.sharedKeys() == 1);
        updateObj(ctx, pack, rows.tmp, OLD_OBJ);
        check("swap, index first: both OBJ# found after the second update",
              byObj(pack, NEW_OBJ) == rows.x4 && byObj(pack, OLD_OBJ) == rows.tmp);
        check("swap, index first: nothing shared after the swap", pack.sharedKeys() == 0);
        check("swap, index first: both names indexed", pack.mapKey.size() == 2);
        pack.clear(&ctx);
        check("swap, index first: clear leaves no key", pack.mapKey.empty() && pack.unorderedMapKey.empty());
    }

    {
        ObjPack pack;
        const Rows rows = load(ctx, pack);
        updateObj(ctx, pack, rows.tmp, OLD_OBJ);
        updateObj(ctx, pack, rows.x4, NEW_OBJ);
        check("swap, rebuilt index first: both OBJ# found after the second update",
              byObj(pack, NEW_OBJ) == rows.x4 && byObj(pack, OLD_OBJ) == rows.tmp && pack.sharedKeys() == 0);
        pack.clear(&ctx);
    }

    {
        // Swap through a temporary OBJ# is unaffected
        ObjPack pack;
        const Rows rows = load(ctx, pack);
        updateObj(ctx, pack, rows.x4, 1);
        updateObj(ctx, pack, rows.tmp, OLD_OBJ);
        updateObj(ctx, pack, rows.x4, NEW_OBJ);
        check("swap through a temporary OBJ#: no key shared on the way",
              byObj(pack, NEW_OBJ) == rows.x4 && byObj(pack, OLD_OBJ) == rows.tmp && pack.sharedKeys() == 0);
        pack.clear(&ctx);
    }

    {
        // The row taking the key is deleted: the key goes back to the row that held it
        ObjPack pack;
        const Rows rows = load(ctx, pack);
        updateObj(ctx, pack, rows.x4, NEW_OBJ);
        const RowId x4RowId = rows.x4->rowId;
        pack.drop(&ctx, x4RowId);
        check("taker deleted: the key is back with the row that held it", byObj(pack, NEW_OBJ) == rows.tmp && pack.sharedKeys() == 0);
        pack.drop(&ctx, rows.tmp->rowId);
        check("taker deleted: deleting the other row leaves no key", pack.unorderedMapKey.empty() && pack.mapKey.empty());
        pack.clear(&ctx);
    }

    {
        // The row that held the key is deleted while shared: the taker keeps it
        ObjPack pack;
        const Rows rows = load(ctx, pack);
        updateObj(ctx, pack, rows.x4, NEW_OBJ);
        pack.drop(&ctx, rows.tmp->rowId);
        check("holder deleted: the taker keeps the key", byObj(pack, NEW_OBJ) == rows.x4 && pack.sharedKeys() == 0);
        pack.clear(&ctx);
    }

    {
        // Still shared when the pack is cleared: clear must not touch the taker's key twice
        ObjPack pack;
        const Rows rows = load(ctx, pack);
        updateObj(ctx, pack, rows.x4, NEW_OBJ);
        pack.clear(&ctx);
        check("clear while shared: no key and no shared row left",
              pack.mapKey.empty() && pack.unorderedMapKey.empty() && pack.sharedKeys() == 0);
    }

    {
        // Unchanged: dropping the keys of a row that was never indexed still stops
        ObjPack pack;
        SysObj* const row = index(5, 42, 42, "NOT_INDEXED");
        pack.add(&ctx, row);
        check("row never indexed: update still stops with 50030 (missing index)", throwsMissing(ctx, pack, row, 43));
        pack.mapRowId.clear();
        delete row;
    }

    Locales locales;
    locales.initialize();
    char pattern[] = "/tmp/olr-shared-key-XXXXXX";
    const char* created = mkdtemp(pattern);
    if (created == nullptr) {
        std::cerr << "FAIL mkdtemp\n";
        return 1;
    }
    const std::string base(created);
    check("checkpoint written while no dictionary transaction runs", writes(ctx, locales, base, 300, 2));
    check("no checkpoint while a dictionary transaction is applied or after one failed", !writes(ctx, locales, base, 310, 3));
    std::filesystem::remove_all(base);

    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "all passed\n";
    return 0;
}
