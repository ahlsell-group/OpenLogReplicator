/* Test the choice of the schema checkpoint for a restart through the checkpoint and position files on disk
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
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../metadata/SerializerJson.h"
#include "../state/StateDisk.h"

// Schema checkpoints at 300, 450 and 480 with stream positions 310, 470 and 500 (the LWN of 480 committed a
// transaction at 500). A restart from 500 must not read from 480: transactions placed at 500 before it are not read
// again. It reads from 450. Written by Metadata::writeCheckpoint, chosen by Metadata::readCheckpoints.
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

    void prepare(Metadata& metadata, const std::string& dir) {
        metadata.state = new StateDisk(metadata.ctx, dir);
        metadata.serializer = new SerializerJson();
        metadata.nlsCharacterSet = "AL32UTF8";
        metadata.nlsNcharCharacterSet = "AL16UTF16";
        metadata.dbTimezoneStr = "+00:00";
        metadata.setNlsCharset(metadata.nlsCharacterSet, metadata.nlsNcharCharacterSet);
    }

    // Each checkpoint at scn C is written with sequence C / 10, so the sequence read back names the checkpoint
    void write(Ctx& ctx, Locales& locales, const std::string& dir, const std::vector<std::pair<uint64_t, uint64_t>>& checkpoints) {
        Metadata metadata(&ctx, &locales, "TEST", Scn::none(), Seq::none(), "", 0);
        prepare(metadata, dir);
        metadata.allowCheckpoints();
        for (const auto& [scn, position]: checkpoints) {
            metadata.sequence = Seq(static_cast<uint32_t>(scn / 10));
            metadata.checkpoint(ctx.parserThread, Scn(scn), Time(0), Seq(static_cast<uint32_t>(scn / 10)), FileOffset(512),
                                0, Seq::none(), FileOffset::zero(), Xid::zero(), Scn(position));
            metadata.writeCheckpoint(ctx.parserThread, true);
        }
    }

    // The checkpoint a start at startScn reads from, by its sequence
    uint32_t restart(Ctx& ctx, Locales& locales, const std::string& dir, uint64_t startScn) {
        Metadata metadata(&ctx, &locales, "TEST", Scn(startScn), Seq::none(), "", 0);
        prepare(metadata, dir);
        metadata.readCheckpoints();
        return metadata.sequence == Seq::none() ? 0 : metadata.sequence.getData();
    }

    std::string fresh(const std::string& base, const std::string& name) {
        const std::string dir = base + "/" + name;
        std::filesystem::create_directories(dir);
        return dir;
    }
}

int main() {
    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    Locales locales;
    locales.initialize();

    char pattern[] = "/tmp/olr-chkpt-position-XXXXXX";
    const char* created = mkdtemp(pattern);
    if (created == nullptr) {
        std::cerr << "FAIL mkdtemp\n";
        return 1;
    }
    const std::string base(created);

    {
        const std::string dir = fresh(base, "three");
        write(ctx, locales, dir, {{300, 310}, {450, 470}, {480, 500}});
        check("position files are written next to the checkpoints",
              std::filesystem::exists(dir + "/TEST-position-480.json") && std::filesystem::exists(dir + "/TEST-chkpt-480.json"));
        check("start at 500: the checkpoint at 480 (position 500) is passed, 450 is read", restart(ctx, locales, dir, 500) == 45);
        check("start at 501: the checkpoint at 480 (position 500) is read", restart(ctx, locales, dir, 501) == 48);
        check("start at 460: the checkpoint at 450 (position 470) is passed, 300 is read", restart(ctx, locales, dir, 460) == 30);

        // A position file emptied by a kill during its write: that checkpoint is tried after the others
        std::ofstream(dir + "/TEST-position-450.json", std::ios::trunc).close();
        check("start at 501 with an empty position file at 450: 480 is read", restart(ctx, locales, dir, 501) == 48);
        check("start at 500 with an empty position file at 450: 300 is read", restart(ctx, locales, dir, 500) == 30);
        std::ofstream(dir + "/TEST-position-300.json", std::ios::trunc) << "{not json";
        check("start at 500, no readable position below it: the newest at or below is read (warning 60049)",
              restart(ctx, locales, dir, 500) == 48);
    }

    {
        // Checkpoints written by an older build: no position files, used as before
        const std::string dir = fresh(base, "old");
        write(ctx, locales, dir, {{300, 310}, {480, 500}});
        std::filesystem::remove(dir + "/TEST-position-300.json");
        std::filesystem::remove(dir + "/TEST-position-480.json");
        check("checkpoints without position files: the newest at or below the start is read", restart(ctx, locales, dir, 500) == 48);
    }

    {
        // A position file whose checkpoint is gone (a crash between the two writes) is removed at start
        const std::string dir = fresh(base, "orphan");
        write(ctx, locales, dir, {{300, 310}});
        std::ofstream(dir + "/TEST-position-999.json") << R"({"database":"TEST","scn":999,"position-scn":1000})";
        restart(ctx, locales, dir, 500);
        check("orphan position file is removed", !std::filesystem::exists(dir + "/TEST-position-999.json") &&
              std::filesystem::exists(dir + "/TEST-position-300.json"));
    }

    std::filesystem::remove_all(base);
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
