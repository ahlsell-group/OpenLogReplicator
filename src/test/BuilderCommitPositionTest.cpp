/* Test that a transaction committed below the stream position is sent again after a restart from that position
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
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Attribute.h"
#include "../common/Clock.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"

// A commits at 500, then T commits at 490 in a later LWN: T is sent at position 500 after A's messages. A client which
// has A and part of T has the offset (500, k) and restarts with START at 500. The parser has to send A and T again with
// the same positions, also T, whose commit SCN is below the start SCN, and it has to read them from a checkpoint taken
// before the stream position reached 500.
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

    class TestClock final : public Clock {
    public:
        time_ut now{1000000000};
        [[nodiscard]] time_ut getTimeUt() const override { return now; }
        [[nodiscard]] time_t getTimeT() const override { return static_cast<time_t>(now / 1000000); }
    };

    struct Commit {
        uint64_t xid;
        uint64_t commitScn;
        typeIdx messages;
    };

    struct Sent {
        uint64_t xid;
        Scn scn;
        typeIdx firstIdx;
        typeIdx lastIdx;

        bool operator==(const Sent& other) const {
            return xid == other.xid && scn == other.scn && firstIdx == other.firstIdx && lastIdx == other.lastIdx;
        }
    };

    // The parser's choice for each commit (Parser::appendToTransactionCommit), then the positions the builder gives
    std::vector<Sent> run(Ctx& ctx, Locales& locales, Metadata& metadata, Format& format, const std::vector<Commit>& commits) {
        BuilderJson builder(&ctx, &locales, &metadata, format, 0);
        builder.initialize();
        AttributeMap attributes;
        attributes[Attribute::KEY::LOGIN_USER_NAME] = "TEST";
        std::vector<Sent> sent;
        for (const Commit& commit: commits) {
            if (!builder.isDataCommit(Scn(commit.commitScn)))
                continue;
            builder.processBegin(Xid(commit.xid, 1, 1), 1, Seq(1), Scn(commit.commitScn - 5), Time(0), Seq(1), Scn(commit.commitScn), Time(0),
                                 &attributes);
            const typeIdx first = builder.lwnIdx + 1;
            builder.lwnIdx += commit.messages;
            sent.push_back({commit.xid, builder.lwnScn, first, builder.lwnIdx});
        }
        return sent;
    }
}

int main() {
    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    TestClock* clock = new TestClock();
    delete ctx.clock;
    ctx.clock = clock;
    Locales locales;
    Metadata metadata(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
    Format format(Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                  Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::DEFAULT, Format::RID_FORMAT::SKIP,
                  Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                  Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                  Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                  Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT);

    // Redo order after the checkpoint the restart reads from. B commits at 495 too, C at 510 moves the position on.
    const std::vector<Commit> commits{{1, 450, 2}, {2, 500, 3}, {3, 490, 4}, {4, 495, 1}, {5, 510, 2}, {6, 505, 1}};

    metadata.firstDataScn = Scn(400);
    const std::vector<Sent> live = run(ctx, locales, metadata, format, commits);
    check("first run sends every transaction", live.size() == 6);
    check("T is sent at A's position after A", live[2] == Sent({3, Scn(500), 4, 7}));
    check("B is sent at A's position after T", live[3] == Sent({4, Scn(500), 8, 8}));
    check("a commit below 510 is sent at 510", live[5] == Sent({6, Scn(510), 3, 3}));

    // START at 500 (an offset in T): every transaction sent at or after 500 again, with the same positions
    metadata.firstDataScn = Scn(500);
    const std::vector<Sent> replay = run(ctx, locales, metadata, format, commits);
    check("START at 500 sends A, T, B, C and the one after C", replay.size() == 5);
    check("START at 500: same positions as the first run",
          replay.size() == 5 && replay[0] == live[1] && replay[1] == live[2] && replay[2] == live[3] && replay[3] == live[4] && replay[4] == live[5]);

    // START at 510: only what was sent at or after 510
    metadata.firstDataScn = Scn(510);
    const std::vector<Sent> after = run(ctx, locales, metadata, format, commits);
    check("START at 510: same positions as the first run", after.size() == 2 && after[0] == live[4] && after[1] == live[5]);

    // Without an inversion nothing changes: every transaction is sent by its commit SCN
    metadata.firstDataScn = Scn(460);
    const std::vector<Commit> ordered{{1, 450, 2}, {2, 460, 3}, {3, 470, 1}};
    const std::vector<Sent> orderedSent = run(ctx, locales, metadata, format, ordered);
    check("no inversion: commits at or after the start SCN only, at their commit SCN",
          orderedSent.size() == 2 && orderedSent[0] == Sent({2, Scn(460), 1, 3}) && orderedSent[1] == Sent({3, Scn(470), 1, 1}));

    // The checkpoint for a restart: the stream position at 480 was already 500 (A committed in the LWN of 480)
    {
        const std::set<Scn> scns{Scn(300), Scn(450), Scn(480), Scn(520)};
        const std::map<Scn, Scn> positions{{Scn(300), Scn(310)}, {Scn(450), Scn(470)}, {Scn(480), Scn(500)}, {Scn(520), Scn(530)}};
        std::vector<Scn> candidates;
        const size_t preferred = Metadata::checkpointCandidates(scns, positions, Scn(500), candidates);
        check("checkpoint whose position reached the start SCN is tried last",
              preferred == 2 && candidates == std::vector<Scn>({Scn(450), Scn(300), Scn(480)}));

        candidates.clear();
        const size_t preferredBelow = Metadata::checkpointCandidates(scns, positions, Scn(501), candidates);
        check("checkpoint with a position below the start SCN is used",
              preferredBelow == 3 && candidates == std::vector<Scn>({Scn(480), Scn(450), Scn(300)}));

        candidates.clear();
        const size_t preferredOld = Metadata::checkpointCandidates(scns, {}, Scn(500), candidates);
        check("checkpoint without a position (older build) is used as before",
              preferredOld == 3 && candidates == std::vector<Scn>({Scn(480), Scn(450), Scn(300)}));

        candidates.clear();
        const size_t preferredNone = Metadata::checkpointCandidates({Scn(480)}, positions, Scn(500), candidates);
        check("only checkpoints past the position: tried as a fallback", preferredNone == 0 && candidates == std::vector<Scn>({Scn(480)}));
    }

    // Inversions are counted; the warning comes at most once a minute
    {
        BuilderJson builder(&ctx, &locales, &metadata, format, 0);
        builder.initialize();
        AttributeMap attributes;
        attributes[Attribute::KEY::LOGIN_USER_NAME] = "TEST";
        builder.processBegin(Xid(1, 1, 1), 1, Seq(1), Scn(490), Time(0), Seq(1), Scn(500), Time(0), &attributes);
        check("commit above the position is not counted", !builder.countCommitBelowPosition(Xid(2, 1, 1), Scn(501)));
        check("commit at the position is not counted", !builder.countCommitBelowPosition(Xid(2, 1, 1), Scn(500)));
        check("commit below the position is counted and warned",
              builder.countCommitBelowPosition(Xid(3, 1, 1), Scn(490)) && builder.commitsBelowPosition == 1 && builder.commitsBelowPositionUnwarned == 0);
        clock->now += 30000000;
        builder.countCommitBelowPosition(Xid(4, 1, 1), Scn(491));
        check("second within a minute is counted, not warned", builder.commitsBelowPosition == 2 && builder.commitsBelowPositionUnwarned == 1);
        clock->now += 31000000;
        builder.countCommitBelowPosition(Xid(5, 1, 1), Scn(492));
        check("after a minute it is warned with the count", builder.commitsBelowPosition == 3 && builder.commitsBelowPositionUnwarned == 0);
    }

    metadata.firstDataScn = Scn::none();
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
