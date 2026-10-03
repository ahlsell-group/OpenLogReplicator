/* Test for the direct-path load warning (code 60042)
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

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "../common/Ctx.h"
#include "../common/RedoLogRecord.h"
#include "../common/exception/RedoLogException.h"
#include "../parser/DirectLoadTracker.h"
#include "../parser/OpCode1301.h"

// A direct-path load (INSERT /*+ APPEND */) is logged as one OP 19.1 "Direct Loader block redo entry" per formatted
// table block, the same opcode a BASICFILE NOCACHE LOB uses for its pages. Field 2 is the block type: 6 for a table data
// block, 40 for a LOB page. For a data block field 1 is the block without its 20-byte cache header, so the segment data
// object id is at offset 4 (after the KTBBH type), while a LOB page has it at offset 0. Layouts taken from a redo dump of
// Oracle 23 Free.
//
// The parser feeds each data block of a replicated table into the tracker with the LWN's redo time. A load is reported
// when no block of the table came for 10 s, every 60 s while it goes on, and at the end of the redo log file, so one load
// gives one warning per table with a block count and scn range, not one per block or LWN.
namespace {
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    bool contains(const std::string& text, const std::string& part) {
        return text.find(part) != std::string::npos;
    }

    // Change vector built from its fields: the field size array first (one unused slot, then one 16-bit size per
    // field), then the fields, each padded to 4 bytes, the way RedoLogRecord::nextField walks them.
    class Vector {
    public:
        std::vector<uint8_t> bytes;
        OpenLogReplicator::RedoLogRecord record{};

        explicit Vector(const std::vector<std::vector<uint8_t>>& fields) {
            const size_t sizesBytes = (fields.size() + 1) * 2;
            const size_t fieldPos = (sizesBytes + 3) & ~static_cast<size_t>(3);
            bytes.assign(fieldPos, 0);
            for (size_t i = 0; i < fields.size(); ++i) {
                const uint16_t size = static_cast<uint16_t>(fields[i].size());
                bytes[(i + 1) * 2] = static_cast<uint8_t>(size & 0xFF);
                bytes[(i + 1) * 2 + 1] = static_cast<uint8_t>(size >> 8);
                bytes.insert(bytes.end(), fields[i].begin(), fields[i].end());
                bytes.resize((bytes.size() + 3) & ~static_cast<size_t>(3), 0);
            }
            record.dataExt = bytes.data();
            record.size = static_cast<uint32_t>(bytes.size());
            record.fieldCnt = static_cast<uint16_t>(fields.size());
            record.fieldSizesDelta = 0;
            record.fieldPos = static_cast<uint16_t>(fieldPos);
            record.opCode = 0x1301;
        }
    };

    // 8 KB block minus the 20-byte cache header and the 4-byte tail
    std::vector<uint8_t> block(const std::vector<uint8_t>& head) {
        std::vector<uint8_t> field(8168, 0);
        for (size_t i = 0; i < head.size(); ++i)
            field[i] = head[i];
        return field;
    }

    bool process(OpenLogReplicator::Ctx& ctx, Vector& v, std::string& error) {
        try {
            OpenLogReplicator::OpCode1301::process1301(&ctx, &v.record);
            return true;
        } catch (OpenLogReplicator::RedoLogException& ex) {
            error = std::to_string(ex.code) + " " + ex.msg;
            return false;
        }
    }
}

int main() {
    using OpenLogReplicator::DirectLoadTracker;
    using OpenLogReplicator::Scn;

    OpenLogReplicator::Ctx ctx;
    ctx.version = OpenLogReplicator::RedoLogRecord::REDO_VERSION_19_0;
    std::string error;

    {
        // table data block of dataobj 73094 (0x11d86): KTBBH type 1, seg/obj, csc, ...
        Vector v({block({0x01, 0x00, 0x00, 0x00, 0x86, 0x1d, 0x01, 0x00, 0xae, 0xb6, 0x22, 0x00, 0x00, 0x80, 0x00, 0x00}), {0x06}});
        check("19.1 data block parses", process(ctx, v, error) || (std::cerr << error << "\n", false));
        check("  is a direct-load data block", v.record.directLoadDataBlock);
        check("  dataobj from the block header", v.record.dataObj == 73094);
    }

    {
        // LOB page of LOB segment dataobj 73107 (0x11d93): dataobj first, then the LOB id
        Vector v({block({0x93, 0x1d, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x0a, 0x2f, 0x6f}), {0x28}});
        check("19.1 LOB page parses", process(ctx, v, error) || (std::cerr << error << "\n", false));
        check("  is not a data block", !v.record.directLoadDataBlock);
        check("  LOB dataobj unchanged", v.record.dataObj == 73107);
    }

    {
        // a LOB page whose dataobj happens to start with byte 1 is still a LOB page: field 2 decides
        Vector v({block({0x01, 0x00, 0x00, 0x00, 0x86, 0x1d, 0x01, 0x00}), {0x28}});
        check("19.1 LOB page with dataobj 1 parses", process(ctx, v, error) || (std::cerr << error << "\n", false));
        check("  is not a data block", !v.record.directLoadDataBlock);
        check("  dataobj 1 kept", v.record.dataObj == 1);
    }

    DirectLoadTracker tracker;
    check("empty at start", tracker.empty());
    check("end of LWN without blocks", tracker.endLwn(1000).empty());
    check("flush of nothing", tracker.flush().empty());

    // t=1000: 3 blocks of T1 (two SCNs), 1 block of T2
    tracker.add(100, 200, "OWN", "T1", Scn(5000), 1000);
    tracker.add(100, 200, "OWN", "T1", Scn(5000), 1000);
    tracker.add(300, 301, "OWN", "T2", Scn(5001), 1000);
    tracker.add(100, 200, "OWN", "T1", Scn(5002), 1000);
    check("not empty after add", !tracker.empty());
    check("t=1000: loads running, no message", tracker.endLwn(1000).empty());

    // other LWNs in between (Oracle writes a load in batches): still no message
    check("t=1005: within the idle time, no message", tracker.endLwn(1005).empty());

    // t=1008: T1 continues, T2 idle for 8 s
    tracker.add(100, 200, "OWN", "T1", Scn(5010), 1008);
    check("t=1008: no message", tracker.endLwn(1008).empty());

    // t=1010: T2 idle for 10 s, T1 for 2 s
    std::vector<std::string> messages = tracker.endLwn(1010);
    check("t=1010: one message, for T2", messages.size() == 1);
    if (messages.size() == 1) {
        std::cout << "     " << messages[0] << "\n";
        check("T2 named", contains(messages[0], "OWN.T2 (obj: 300, dataobj: 301,"));
        check("T2 single scn", contains(messages[0], "scn: 5001,"));
        check("T2 block count", contains(messages[0], "blocks: 1)"));
        check("says the rows are missing", contains(messages[0], "missing from the output"));
    }

    // t=1018: T1 idle for 10 s
    messages = tracker.endLwn(1018);
    check("t=1018: one message, for T1", messages.size() == 1);
    check("empty after both loads ended", tracker.empty());
    if (messages.size() == 1) {
        std::cout << "     " << messages[0] << "\n";
        check("T1 named", contains(messages[0], "OWN.T1 (obj: 100, dataobj: 200,"));
        check("T1 scn range over the batches", contains(messages[0], "scn: 5000-5010,"));
        check("T1 block count over the batches", contains(messages[0], "blocks: 4)"));
    }

    // a load that goes on is reported every MAX_S seconds
    for (int64_t t = 2000; t < 2000 + DirectLoadTracker::MAX_S; t += 5) {
        tracker.add(100, 200, "OWN", "T1", Scn(7000 + t), t);
        check("long load: no message before MAX_S", tracker.endLwn(t).empty());
    }
    tracker.add(100, 200, "OWN", "T1", Scn(9000), 2000 + DirectLoadTracker::MAX_S);
    messages = tracker.endLwn(2000 + DirectLoadTracker::MAX_S);
    check("long load: message after MAX_S", messages.size() == 1);
    if (messages.size() == 1)
        check("long load: 13 blocks so far", contains(messages[0], "blocks: 13)"));
    check("long load: counting starts again", tracker.empty());

    // a load still running at the end of the redo log file is reported then
    tracker.add(100, 200, "OWN", "T1", Scn(6000), 3000);
    check("end of file: LWN keeps it", tracker.endLwn(3000).empty());
    messages = tracker.flush();
    check("end of file: one message", messages.size() == 1);
    if (messages.size() == 1)
        check("end of file: count restarted", contains(messages[0], "scn: 6000, blocks: 1)"));
    check("empty after flush", tracker.empty());

    check("warning code", DirectLoadTracker::WARNING_CODE == 60042);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
