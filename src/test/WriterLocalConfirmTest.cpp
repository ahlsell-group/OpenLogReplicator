/* Test that messages the writer confirms itself do not wait behind messages the client has not confirmed
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
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../writer/Writer.h"

// A checkpoint the client does not get is confirmed by the writer itself. A client like Debezium keeps the last
// transaction it received unconfirmed until a later one arrives; while no captured table changes, every LWN adds a
// checkpoint after it. Those checkpoints must not take places in the queue, or the queue fills and the writer stops.
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

        void sent(BuilderMsg* msg) { createMessage(msg); }
        void local(BuilderMsg* msg) { confirmLocalMessage(msg); }
        void reset() { resetMessageQueue(); }
        uint64_t queued() const { return currentQueueSize; }
        Scn confirmed() const { return confirmedScn; }
        typeIdx confirmedIndex() const { return confirmedIdx; }

    protected:
        void sendMessage(BuilderMsg*) override {}
        std::string getType() const override { return "test"; }
        void pollQueue() override {}
    };

    // Walks the builder's buffers like the writer: returns the message after the last one returned. The cursor stays in
    // the newest buffer, which a release never frees while it still holds a message after the confirmed one.
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
    };
}

int main() {
    Ctx ctx;
    TestThread thread(&ctx);
    ctx.parserThread = &thread;
    ctx.initialize(32, 64, 16, 16, 0, 0, 16, 16);
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

    Cursor cursor;
    auto emit = [&](uint64_t scn) {
        builder.processCheckpoint(Seq(1), Scn(scn), Time(0), FileOffset(0), false);
        return cursor.next(builder);
    };

    // Nothing waits for the client: a local message moves the confirmed position at once
    BuilderMsg* alone = emit(100);
    writer.local(alone);
    check("local message with an empty queue is confirmed at once",
          writer.queued() == 0 && writer.confirmed() == Scn(100) && writer.confirmedIndex() == alone->lwnIdx);

    // The client holds the message at 200; idle LWNs follow, many times queue-size and over several builder buffers
    BuilderMsg* held = emit(200);
    writer.sent(held);
    const uint64_t firstBuffer = builder.firstBuilderQueue->id;
    constexpr uint64_t IDLE = 20000;
    BuilderMsg* last = nullptr;
    for (uint64_t scn = 300; scn < 300 + IDLE; ++scn) {
        last = emit(scn);
        writer.local(last);
    }
    const typeIdx lastIdx = last->lwnIdx;
    const uint64_t lastBuffer = last->queueId;
    check("idle LWNs span several builder buffers", lastBuffer > firstBuffer + 1);
    check("local messages behind an unconfirmed one take no place in the queue", writer.queued() == 1);
    check("confirmed position stays before the unconfirmed message", writer.confirmed() == Scn(100));
    check("buffers after the unconfirmed message are kept", builder.firstBuilderQueue->id == firstBuffer);

    // Once the client confirms it, the position moves past the idle LWNs and their buffers are released
    writer.confirmMessage(held);
    check("queue empty after the client confirms", writer.queued() == 0);
    check("confirmed position moves to the last local message",
          writer.confirmed() == Scn(300 + IDLE - 1) && writer.confirmedIndex() == lastIdx);
    check("buffers before the last local message are released", builder.firstBuilderQueue->id == lastBuffer);

    // Local messages between and after two sent ones: each run is taken once every message before it is confirmed
    BuilderMsg* first = emit(50000);
    writer.sent(first);
    writer.local(emit(50001));
    writer.local(emit(50002));
    BuilderMsg* second = emit(50003);
    writer.sent(second);
    writer.local(emit(50004));
    check("two runs of local messages, only the sent ones queued", writer.queued() == 2);
    writer.confirmMessage(first);
    check("first confirmed: position at the end of the run before the second",
          writer.queued() == 1 && writer.confirmed() == Scn(50002));
    writer.confirmMessage(second);
    check("second confirmed: position at the run after it", writer.queued() == 0 && writer.confirmed() == Scn(50004));

    // The client confirms the later message first, as a Kafka producer can: nothing moves until the earlier one is
    BuilderMsg* early = emit(60000);
    writer.sent(early);
    writer.local(emit(60001));
    BuilderMsg* late = emit(60002);
    writer.sent(late);
    writer.local(emit(60003));
    writer.confirmMessage(late);
    check("later message confirmed first: position unchanged", writer.queued() == 2 && writer.confirmed() == Scn(50004));
    writer.confirmMessage(early);
    check("earlier message confirmed: both runs taken", writer.queued() == 0 && writer.confirmed() == Scn(60003));

    // A reconnect drops the queue: a position held before it must not be taken afterwards
    BuilderMsg* dropped = emit(70000);
    writer.sent(dropped);
    writer.local(emit(70001));
    writer.reset();
    BuilderMsg* again = emit(70002);
    writer.sent(again);
    writer.confirmMessage(again);
    check("position held before a reconnect is not taken after it", writer.confirmed() == Scn(70002));

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
