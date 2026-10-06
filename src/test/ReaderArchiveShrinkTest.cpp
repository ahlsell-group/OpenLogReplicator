/* Test for an archived redo log which is shorter than its header says, or shrinks while it is read
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

#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

#include "../common/Ctx.h"
#include "../common/types/FileOffset.h"
#include "../reader/ReaderFilesystem.h"

// A backup tool may truncate or delete an archived redo log while OpenLogReplicator reads it, or a copy may be short.
// An early end of file is a read error: taken for the end of the log, it would report the log as finished and the
// replicator would go on with the next sequence, skipping the lost redo. The test writes a synthetic archived log:
// a 2-block file header with the block count and next scn, and empty data blocks.
using namespace OpenLogReplicator;

namespace {
    constexpr uint BLOCK = 512;
    constexpr uint32_t SEQ = 100;
    int failures = 0;

    void check(const std::string& name, bool ok) {
        if (!ok) {
            std::cerr << "FAIL " << name << "\n";
            ++failures;
        } else
            std::cout << "ok   " << name << "\n";
    }

    void put32(uint8_t* p, uint32_t v) {
        p[0] = v & 0xFF;
        p[1] = (v >> 8) & 0xFF;
        p[2] = (v >> 16) & 0xFF;
        p[3] = (v >> 24) & 0xFF;
    }

    // An archived redo log of headerBlocks blocks (as its header says), written with fileBlocks blocks
    void writeArchive(const std::string& path, uint32_t headerBlocks, uint32_t fileBlocks) {
        std::vector<uint8_t> data(static_cast<size_t>(fileBlocks) * BLOCK, 0);
        uint8_t* b0 = data.data();
        b0[1] = 0x22;
        put32(b0 + 20, BLOCK);
        b0[28] = 0x7D;
        b0[29] = 0x7C;
        b0[30] = 0x7B;
        b0[31] = 0x7A;

        for (uint32_t blk = 1; blk < fileBlocks; ++blk) {
            uint8_t* b = data.data() + static_cast<size_t>(blk) * BLOCK;
            b[0] = 1;
            b[1] = 0x22;
            put32(b + 4, blk);
            put32(b + 8, SEQ);
        }

        uint8_t* b1 = data.data() + BLOCK;
        put32(b1 + 20, 0x13000000);   // 19.0
        put32(b1 + 52, 1);            // activation
        put32(b1 + 156, headerBlocks);
        put32(b1 + 160, 1);           // resetlogs
        b1[176] = 1;                  // thread
        put32(b1 + 180, 1000);        // first scn
        put32(b1 + 192, 2000);        // next scn: a complete archived log

        const int fd = open(path.c_str(), O_CREAT | O_TRUNC | O_WRONLY, 0600);
        if (fd < 0 || write(fd, data.data(), data.size()) != static_cast<ssize_t>(data.size())) {
            std::cerr << "cannot write " << path << "\n";
            exit(2);
        }
        close(fd);
    }

    // Reads the log the way the replicator does and returns the reader's final code; between open and read, `between`
    // runs (to damage the file). Returns OK if the file could not be opened (open failure is a loud error, too).
    Reader::REDO_CODE readArchive(const std::string& path, bool& opened, void (*between)(const std::string&)) {
        Ctx ctx;
        ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
        ctx.flags |= static_cast<uint>(Ctx::REDO_FLAGS::DIRECT_DISABLE);
        ctx.disableChecks |= static_cast<uint>(Ctx::DISABLE_CHECKS::BLOCK_SUM);
        ctx.archReadTries = 1;
        ctx.archReadSleepUs = 1000;
        ctx.redoReadSleepUs = 1000;

        auto* reader = new ReaderFilesystem(&ctx, "test-reader", "TEST", 0, false);
        reader->initialize();
        ctx.spawnThread(reader);
        reader->fileName = path;

        opened = reader->checkRedoLog() && reader->updateRedoLog();
        Reader::REDO_CODE ret = Reader::REDO_CODE::OK;
        if (opened) {
            if (between != nullptr)
                between(path);
            reader->setStatusRead();
            for (int i = 0; i < 500; ++i) {
                ret = reader->getRet();
                if (ret != Reader::REDO_CODE::OK)
                    break;
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        ctx.stopSoft();
        reader->wakeUp();
        ctx.finishThread(reader);
        delete reader;
        return ret;
    }

    void truncateToHalf(const std::string& path) {
        if (truncate(path.c_str(), 20 * BLOCK) != 0)
            exit(2);
    }

    void removeFile(const std::string& path) {
        unlink(path.c_str());
    }
}

int main() {
    const std::string path = "ReaderArchiveShrinkTest-" + std::to_string(getpid()) + ".arc";
    bool opened = false;

    // Control: an intact archived log is read to its end and reported as finished
    writeArchive(path, 40, 40);
    Reader::REDO_CODE ret = readArchive(path, opened, nullptr);
    check("intact archived log opens", opened);
    check("intact archived log is read to the end (FINISHED)", ret == Reader::REDO_CODE::FINISHED);

    // The file is 20 blocks, its header says 40: it must not be opened for reading
    writeArchive(path, 40, 20);
    ret = readArchive(path, opened, nullptr);
    check("archived log shorter than its header is refused (40012)", !opened);

    // The file is truncated after it was opened: the reader must fail, not report FINISHED
    writeArchive(path, 40, 40);
    ret = readArchive(path, opened, truncateToHalf);
    check("archived log opens before it is truncated", opened);
    check("archived log truncated while read ends with a read error (40013), not FINISHED", ret == Reader::REDO_CODE::ERROR_READ);

    // Deleted after open on a local filesystem: the open descriptor keeps the data, the log is read in full
    writeArchive(path, 40, 40);
    ret = readArchive(path, opened, removeFile);
    check("archived log removed after open is still read in full on a local filesystem", opened && ret == Reader::REDO_CODE::FINISHED);

    unlink(path.c_str());
    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
