/* Test that messages at or below the position a client confirmed are not sent to it again
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

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../writer/Writer.h"

// After a reconnect the server sends a transaction again from its start, and a client which had it up to (scn, idx)
// confirms that position at once. The messages already queued are released; the ones read after the CONFIRM must be
// confirmed locally too, not sent, or a resend longer than queue-size waits for a second CONFIRM of the same position.
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

    class TestWriter final : public Writer {
    public:
        TestWriter(Ctx* newCtx, Builder* newBuilder, Metadata* newMetadata): Writer(newCtx, "test-writer", "TEST", newBuilder, newMetadata) {}

        std::vector<typeIdx> sentIdx;

        // The writer's choice for a message read from the buffers (Writer::mainLoop)
        void read(BuilderMsg* msg) {
            if (!metadata->isNewData(msg->lwnScn, msg->lwnIdx))
                confirmLocalMessage(msg);
            else {
                createMessage(msg);
                sentIdx.push_back(msg->lwnIdx);
            }
        }
        void clientConfirms(Scn scn, typeIdx idx) { confirmClient(scn, idx); }
        // What WriterStream::processContinue does with a client position
        void clientContinues(Scn scn, typeIdx idx) {
            metadata->clientScn = scn;
            metadata->clientIdx = idx;
            resetMessageQueue();
        }
        uint64_t queued() const { return currentQueueSize; }
        Scn confirmed() const { return confirmedScn; }
        typeIdx confirmedIndex() const { return confirmedIdx; }

    protected:
        void sendMessage(BuilderMsg*) override {}
        std::string getType() const override { return "test"; }
        void pollQueue() override {}
    };

    // Walks the builder's buffers like the writer
    struct Cursor {
        BuilderQueue* queue{nullptr};
        uint64_t offset{0};

        BuilderMsg* next(const Builder& builder) {
            if (queue == nullptr)
                queue = builder.firstBuilderQueue;
            while (offset + sizeof(BuilderMsg) > queue->confirmedSize || reinterpret_cast<BuilderMsg*>(queue->data + offset)->size == 0) {
                queue = queue->next;
                offset = 0;
            }
            auto* msg = reinterpret_cast<BuilderMsg*>(queue->data + offset);
            offset += sizeof(BuilderMsg) + ((msg->size + 7) & 0xFFFFFFFFFFFFFFF8);
            return msg;
        }

        void rewind(const Builder& builder) {
            queue = builder.firstBuilderQueue;
            offset = queue->start;
        }
    };
}

int main() {
    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
    // Checkpoint messages stand in for the rows of a transaction; they are written to the buffers only with SHOW_CHECKPOINT
    ctx.flags |= static_cast<uint>(Ctx::REDO_FLAGS::SHOW_CHECKPOINT);
    ctx.queueSize = 100;
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
    TestWriter writer(&ctx, &builder, &metadata);
    writer.initialize();

    // Checkpoint messages provide the buffers; each gets the position of a row set on the message
    uint64_t builderScn = 1;
    Cursor cursor;

    // Transaction at 500 with rows 1..8, written before the client connects
    std::vector<BuilderMsg*> rows;
    for (typeIdx idx = 1; idx <= 8; ++idx) {
        builder.processCheckpoint(Seq(1), Scn(builderScn++), Time(0), FileOffset(0), false);
        BuilderMsg* msg = cursor.next(builder);
        msg->lwnScn = Scn(500);
        msg->lwnIdx = idx;
        rows.push_back(msg);
    }

    // START after a server restart: nothing to skip. Rows 1..3 are queued when the client confirms its offset (500, 5)
    for (int i = 0; i < 3; ++i)
        writer.read(rows[i]);
    check("rows 1..3 sent", writer.sentIdx == std::vector<typeIdx>({1, 2, 3}) && writer.queued() == 3);
    writer.clientConfirms(Scn(500), 5);
    check("CONFIRM releases the queued rows", writer.queued() == 0 && writer.confirmed() == Scn(500) && writer.confirmedIndex() == 3);
    check("CONFIRM moves the client position", metadata.clientScn == Scn(500) && metadata.clientIdx == 5);
    for (int i = 3; i < 8; ++i)
        writer.read(rows[i]);
    check("rows 4 and 5 are confirmed locally, not sent again", writer.sentIdx == std::vector<typeIdx>({1, 2, 3, 6, 7, 8}));
    check("confirmed position reaches the client's", writer.queued() == 3 && writer.confirmed() == Scn(500) && writer.confirmedIndex() == 5);

    // A lower CONFIRM never moves the client position back
    writer.clientConfirms(Scn(500), 0);
    check("lower CONFIRM keeps the client position", metadata.clientScn == Scn(500) && metadata.clientIdx == 5 && writer.queued() == 3);
    writer.clientConfirms(Scn::none(), 0);
    check("CONFIRM without a position is ignored", metadata.clientScn == Scn(500) && metadata.clientIdx == 5);

    // CONTINUE before the confirmed position (a client which lost what it had) resets it: rows 3.. are sent again
    writer.clientContinues(Scn(500), 2);
    writer.sentIdx.clear();
    cursor.rewind(builder);
    for (int i = 0; i < 8; ++i)
        writer.read(cursor.next(builder));
    check("CONTINUE resets the position, rows 3..8 are sent again", writer.sentIdx == std::vector<typeIdx>({3, 4, 5, 6, 7, 8}));

    // A client which confirms (scn, 0) only when the scn grows (Debezium up to 3.7.0.Final-ahlsell.11) gets every row of
    // that scn: the position before the scn's first index changes nothing
    writer.clientContinues(Scn(500), 8);
    writer.sentIdx.clear();
    std::vector<BuilderMsg*> next;
    for (typeIdx idx = 1; idx <= 4; ++idx) {
        builder.processCheckpoint(Seq(1), Scn(builderScn++), Time(0), FileOffset(0), false);
        BuilderMsg* msg = cursor.next(builder);
        msg->lwnScn = Scn(600);
        msg->lwnIdx = idx;
        next.push_back(msg);
    }
    writer.read(next[0]);
    writer.read(next[1]);
    writer.clientConfirms(Scn(600), 0);
    check("(scn, 0) releases nothing of that scn", writer.queued() == 2);
    writer.read(next[2]);
    writer.read(next[3]);
    check("(scn, 0) skips nothing of that scn", writer.sentIdx == std::vector<typeIdx>({1, 2, 3, 4}) && writer.queued() == 4);
    check("(scn, 0) is the client position", metadata.clientScn == Scn(600) && metadata.clientIdx == 0);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
