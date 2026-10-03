/* Test for the starting position chosen without a checkpoint
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
#include <vector>

#include "../replicator/ColdStart.h"

// Redo logs in the test: seq 10 holds scn 1000-1999, seq 11 2000-2999, ... seq 19 10000-10999 (current).
// firstSequenceAvailable: logs below it were deleted from the archive.
namespace {
    using OpenLogReplicator::ColdStart;
    using OpenLogReplicator::Scn;
    using OpenLogReplicator::Seq;
    using OpenLogReplicator::Xid;

    int failures = 0;
    int lookups = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    std::function<ColdStart::RedoPosition(Scn)> redo(uint32_t firstSequenceAvailable) {
        return [firstSequenceAvailable](Scn scn) {
            ++lookups;
            ColdStart::RedoPosition position;
            if (scn.getData() < 1000)
                return position;
            position.sequence = Seq(static_cast<uint32_t>(9 + (scn.getData() / 1000)));
            position.available = position.sequence.getData() >= firstSequenceAvailable;
            return position;
        };
    }

    ColdStart::OpenTransaction tx(uint32_t sqn, uint64_t startScn, uint64_t ageS) {
        return ColdStart::OpenTransaction{Xid(1, 2, sqn), Scn(startScn), ageS};
    }
}

int main() {
    const Scn firstData(10500);

    {
        const ColdStart::Choice c = ColdStart::choose({}, firstData, 0, redo(10));
        check("no open transaction: start at the starting scn", c.positionScn == firstData && c.sequence == Seq(19) && c.lost.empty());
    }

    {
        // Oldest open transaction began in seq 12, all logs available: start there, nothing lost
        const ColdStart::Choice c = ColdStart::choose({tx(2, 9000, 60), tx(1, 3500, 600)}, firstData, 0, redo(10));
        check("available: start at the oldest begin", c.positionScn == Scn(3500) && c.sequence == Seq(12));
        check("available: nothing lost", c.lost.empty());
    }

    {
        // Begin of the oldest one is in seq 12, archive only from seq 15: the next one (seq 16) is the start, the oldest is lost
        const ColdStart::Choice c = ColdStart::choose({tx(1, 3500, 600), tx(2, 7200, 120), tx(3, 8100, 60)}, firstData, 0, redo(15));
        check("archive gone: start at the oldest begin with redo", c.positionScn == Scn(7200) && c.sequence == Seq(16));
        check("archive gone: the oldest is named lost", c.lost.size() == 1 && c.lost[0].transaction.xid == Xid(1, 2, 1));
        check("archive gone: reason names the sequence", c.lost.size() == 1 && c.lost[0].reason.find("sequence 12") != std::string::npos);
    }

    {
        // No open transaction has redo left: previous behaviour, start at the starting scn, all older ones lost
        const ColdStart::Choice c = ColdStart::choose({tx(1, 3500, 600), tx(2, 4200, 500)}, firstData, 0, redo(19));
        check("nothing available: start at the starting scn", c.positionScn == firstData && c.sequence == Seq(19));
        check("nothing available: both named lost", c.lost.size() == 2);
    }

    {
        // A transaction with no redo log known at all (scn below the oldest log)
        const ColdStart::Choice c = ColdStart::choose({tx(1, 500, 600)}, firstData, 0, redo(10));
        check("unknown log: start at the starting scn", c.positionScn == firstData);
        check("unknown log: named lost", c.lost.size() == 1 && c.lost[0].reason == "no redo log found for its begin");
    }

    {
        // Age bound: 3600 s; the week-old idle transaction is skipped, the next one used
        const ColdStart::Choice c = ColdStart::choose({tx(1, 1500, 604800), tx(2, 6000, 300)}, firstData, 3600, redo(10));
        check("max age: start at the youngest qualifying begin", c.positionScn == Scn(6000) && c.sequence == Seq(15));
        check("max age: week-old transaction named lost", c.lost.size() == 1 && c.lost[0].reason.find("cold-start-max-age-s 3600") != std::string::npos);
    }

    {
        // Age bound 0 = unlimited
        const ColdStart::Choice c = ColdStart::choose({tx(1, 1500, 604800)}, firstData, 0, redo(10));
        check("max age 0: no limit", c.positionScn == Scn(1500) && c.sequence == Seq(10) && c.lost.empty());
    }

    {
        // Skipped transaction in the same log as the start: read completely anyway, not lost
        const ColdStart::Choice c = ColdStart::choose({tx(1, 10100, 604800)}, firstData, 3600, redo(10));
        check("same log as the start: not lost", c.positionScn == firstData && c.sequence == Seq(19) && c.lost.empty());
    }

    {
        // Transactions which began at or after the starting scn do not move the position
        const ColdStart::Choice c = ColdStart::choose({tx(1, 10600, 1), tx(2, 10500, 1)}, firstData, 0, redo(10));
        check("began after the start: position unchanged", c.positionScn == firstData && c.lost.empty());
    }

    {
        // Each scn is looked up once
        lookups = 0;
        ColdStart::choose({tx(1, 3500, 600), tx(2, 3600, 600), tx(3, 9000, 60)}, firstData, 0, redo(13));
        check("lookups cached", lookups == 3);
    }

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
