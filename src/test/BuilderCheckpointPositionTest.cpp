/* Test that a checkpoint never moves the message position (c_scn, c_idx) back
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
#include <string>

#include "../builder/BuilderJson.h"
#include "../common/Attribute.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// T1 commits at SCN 462 inside the LWN with SCN 455, then the checkpoint for LWN 455 is built, then T2 commits at 462
// too. The checkpoint must not move the position back to 455, or T2 restarts the index at 462 and reuses positions
// T1 already had: a client that continues from T1's last position would then skip T2's first messages. The checkpoint
// must not take an index either: after a restart from a start SCN above 455 it is not sent, and T2's positions must
// be the same as before.
namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
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
}

int main() {
    using namespace OpenLogReplicator;

    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    // Checkpoint messages are written to the buffers only with SHOW_CHECKPOINT
    ctx.flags |= static_cast<uint>(Ctx::REDO_FLAGS::SHOW_CHECKPOINT);
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

    AttributeMap attributes;
    attributes[Attribute::KEY::LOGIN_USER_NAME] = "TEST";

    // T1 commits at 462 and has used three positions at that scn
    builder.processBegin(Xid(1, 1, 1), 1, Seq(1), Scn(450), Time(0), Seq(1), Scn(462), Time(0), &attributes);
    builder.lwnIdx = 3;
    const Scn t1Scn = builder.lwnScn;
    const typeIdx t1LastIdx = builder.lwnIdx;

    // Checkpoint for the LWN T1 committed in
    builder.processCheckpoint(Seq(1), Scn(455), Time(0), FileOffset(0), false);
    const auto* chkpt = reinterpret_cast<const BuilderMsg*>(builder.firstBuilderQueue->data);
    check("checkpoint keeps the position at T1's commit scn", builder.lwnScn == t1Scn);
    check("checkpoint message is not positioned before T1", chkpt->lwnScn == t1Scn && chkpt->lwnIdx >= t1LastIdx);

    // T2 commits at the same scn in the next LWN: its positions follow T1's
    builder.processBegin(Xid(2, 2, 2), 1, Seq(1), Scn(458), Time(0), Seq(1), Scn(462), Time(0), &attributes);
    check("T2 keeps the position scn", builder.lwnScn == t1Scn);
    check("checkpoint takes no index of its own", chkpt->lwnIdx == t1LastIdx && builder.lwnIdx == t1LastIdx);

    // A client that received all of T1 gets T2's first message
    metadata.clientScn = t1Scn;
    metadata.clientIdx = t1LastIdx;
    check("T2 is new data for a client at T1's last position", metadata.isNewData(builder.lwnScn, builder.lwnIdx + 1));

    // A checkpoint at a higher scn still moves the position forward
    builder.processCheckpoint(Seq(1), Scn(470), Time(0), FileOffset(0), false);
    check("checkpoint at a higher scn moves the position forward", builder.lwnScn == Scn(470));

    // T3 commits at a lower scn in a later LWN: its positions follow, they are not reused at 465
    builder.processBegin(Xid(3, 3, 3), 1, Seq(1), Scn(466), Time(0), Seq(1), Scn(475), Time(0), &attributes);
    builder.lwnIdx = 2;
    const typeIdx t3LastIdx = builder.lwnIdx;
    builder.processBegin(Xid(4, 4, 4), 1, Seq(1), Scn(464), Time(0), Seq(1), Scn(465), Time(0), &attributes);
    check("lower commit scn keeps the position scn", builder.lwnScn == Scn(475));
    check("lower commit scn continues the index", builder.lwnIdx == t3LastIdx);
    metadata.clientScn = Scn(475);
    metadata.clientIdx = t3LastIdx;
    check("T4 is new data for a client at T3's last position", metadata.isNewData(builder.lwnScn, builder.lwnIdx + 1));

    // The same T1 and T2 without the checkpoint (a restart with a start SCN above 455) give T2 the same positions
    {
        BuilderJson replay(&ctx, &locales, &metadata, format, 0);
        replay.initialize();
        replay.processBegin(Xid(1, 1, 1), 1, Seq(1), Scn(450), Time(0), Seq(1), Scn(462), Time(0), &attributes);
        replay.lwnIdx = 3;
        replay.processBegin(Xid(2, 2, 2), 1, Seq(1), Scn(458), Time(0), Seq(1), Scn(462), Time(0), &attributes);

        BuilderJson live(&ctx, &locales, &metadata, format, 0);
        live.initialize();
        live.processBegin(Xid(1, 1, 1), 1, Seq(1), Scn(450), Time(0), Seq(1), Scn(462), Time(0), &attributes);
        live.lwnIdx = 3;
        live.processCheckpoint(Seq(1), Scn(455), Time(0), FileOffset(0), false);
        live.processBegin(Xid(2, 2, 2), 1, Seq(1), Scn(458), Time(0), Seq(1), Scn(462), Time(0), &attributes);
        check("T2 positions do not depend on the checkpoint being sent", live.lwnScn == replay.lwnScn && live.lwnIdx == replay.lwnIdx);
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
