/* Test for the writer confirming checkpoints that the builder did not write
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

#include <atomic>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../writer/Writer.h"

// Without SHOW_CHECKPOINT the builder keeps only the position of a checkpoint (Builder::hideCheckpoint). The writer must
// confirm it where it would have read the message: never before a message written earlier, and with the same effect on
// the confirmed position as the message had (fix/queue-checkpoint-stall). A client that keeps a transaction unconfirmed
// must not make the builder or the writer grow while no captured table changes.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        static void transaction(Builder& builder, uint32_t xid, Scn commitScn, const AttributeMap* attributes) {
            builder.processBegin(Xid(1, 1, xid), 1, Seq(1), commitScn, Time(0), Seq(1), commitScn, Time(0), attributes);
            builder.processBeginMessage(Seq(1), Time(0));
            builder.processCommit();
        }

        static uint64_t hiddenSize(Builder& builder) {
            std::unique_lock const lck(builder.mtx);
            return builder.hiddenCheckpoints.size();
        }
    };
}

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

    bool before(Scn scnA, typeIdx idxA, Scn scnB, typeIdx idxB) {
        return scnA < scnB || (scnA == scnB && idxA < idxB);
    }

    class TestWriter final : public Writer {
    public:
        TestWriter(Ctx* newCtx, Builder* newBuilder, Metadata* newMetadata): Writer(newCtx, "test-writer", "TEST", newBuilder, newMetadata) {}

        // Calls as mainLoop makes them
        void sent(BuilderMsg* msg) { createMessage(msg); }
        void local(BuilderMsg* msg) { confirmLocalMessage(msg); }
        bool hidden(uint64_t beforeId) { return confirmHiddenCheckpoints(beforeId); }
        bool idle() { return confirmHiddenCheckpoints(builder->hiddenEndId); }
        void reset() { resetMessageQueue(); }
        uint64_t queued() const { return currentQueueSize; }
        uint64_t held() const { return localHeld.size(); }
        Scn confirmed() const { return confirmedScn; }
        typeIdx confirmedIndex() const { return confirmedIdx; }
        Scn messageConfirmed() const { return messageConfirmedScn; }
        bool redoSet() const { return redo; }
        void clearRedo() { redo = false; }

        // Threaded run: the real mainLoop, the client confirms what it got (all but the last one when holdLast is set)
        bool holdLast{false};
        bool ahead{false};
        std::vector<std::pair<Scn, typeIdx>> received;
        std::vector<std::pair<Scn, typeIdx>> checkpoints;
        BuilderMsg* last{nullptr};

        void loop() {
            builderQueue = builder->firstBuilderQueue;
            oldSize = 0;
            streaming = true;
            mainLoop();
        }

        void releaseLast() {
            if (last != nullptr)
                confirmMessage(last);
            last = nullptr;
        }

    protected:
        void sendMessage(BuilderMsg* msg) override {
            // The confirmed position must stay before every message the client did not get yet
            if (!before(confirmedScn, confirmedIdx, msg->lwnScn, msg->lwnIdx) && confirmedScn != Scn::none())
                ahead = true;
            received.emplace_back(msg->lwnScn, msg->lwnIdx);
            if (holdLast) {
                if (last != nullptr)
                    confirmMessage(last);
                last = msg;
            } else
                confirmMessage(msg);
        }
        std::string getType() const override { return "test"; }
        void pollQueue() override {}
        void writeCheckpoint(bool force) override {
            redo = false;
            if (force && confirmedScn != Scn::none())
                checkpoints.emplace_back(confirmedScn, confirmedIdx);
        }
    };

    // Walks the builder's buffers like the writer
    struct Cursor {
        BuilderQueue* queue{nullptr};
        uint64_t offset{0};

        BuilderMsg* next(const Builder& builder) {
            if (queue == nullptr)
                queue = builder.firstBuilderQueue;
            while (offset + sizeof(BuilderMsg) > queue->confirmedSize || reinterpret_cast<BuilderMsg*>(queue->data + offset)->size == 0) {
                if (queue->next == nullptr)
                    return nullptr;
                queue = queue->next;
                offset = 0;
            }
            auto* msg = reinterpret_cast<BuilderMsg*>(queue->data + offset);
            offset += sizeof(BuilderMsg) + ((msg->size + 7) & 0xFFFFFFFFFFFFFFF8);
            return msg;
        }
    };

    Format makeFormat() {
        return {Format::DB_FORMAT::DEFAULT, Format::ATTRIBUTES_FORMAT::DEFAULT, Format::INTERVAL_DTS_FORMAT::UNIX_NANO,
                Format::INTERVAL_YTM_FORMAT::MONTHS, Format::MESSAGE_FORMAT::FULL, Format::RID_FORMAT::SKIP,
                Format::REDO_THREAD_FORMAT::SKIP, Format::XID_FORMAT::TEXT_HEX, Format::TIMESTAMP_FORMAT::UNIX_NANO,
                Format::TIMESTAMP_FORMAT::UNIX_NANO, Format::TIMESTAMP_TZ_FORMAT::UNIX_NANO_STRING, Format::TIMESTAMP_TYPE::DEFAULT,
                Format::CHAR_FORMAT::UTF8, Format::SCN_FORMAT::NUMERIC, Format::SCN_TYPE::DEFAULT, Format::UNKNOWN_FORMAT::QUESTION_MARK,
                Format::SCHEMA_FORMAT::DEFAULT, Format::COLUMN_FORMAT::CHANGED, Format::UNKNOWN_TYPE::HIDE, Format::USER_TYPE::DEFAULT};
    }

    struct Side {
        Ctx ctx;
        TestThread thread{&ctx};
        Locales locales;
        Format format{makeFormat()};
        std::unique_ptr<Metadata> metadata;
        std::unique_ptr<BuilderJson> builder;
        std::unique_ptr<TestWriter> writer;
        Cursor cursor;
        AttributeMap attributes;

        explicit Side(bool show) {
            ctx.parserThread = &thread;
            ctx.initialize(32, 512, 16, 16, 0, 0, 256, 16);
            ctx.queueSize = 1000;
            ctx.pollIntervalUs = 100;
            if (show)
                ctx.flags |= static_cast<uint>(Ctx::REDO_FLAGS::SHOW_CHECKPOINT);
            metadata = std::make_unique<Metadata>(&ctx, &locales, "TEST", Scn::zero(), Seq::zero(), "", 0);
            builder = std::make_unique<BuilderJson>(&ctx, &locales, metadata.get(), format, 0);
            builder->initialize();
            writer = std::make_unique<TestWriter>(&ctx, builder.get(), metadata.get());
            writer->initialize();
            attributes[Attribute::KEY::LOGIN_USER_NAME] = "TEST";
        }

        void checkpoint(uint64_t scn, bool redo = false) {
            builder->processCheckpoint(Seq(1), Scn(scn), Time(0), FileOffset(0), redo);
        }

        void transaction(uint32_t xid, uint64_t scn) {
            BuilderTestAccess::transaction(*builder, xid, Scn(scn), &attributes);
        }

        // The writer reads everything written: written checkpoints are confirmed locally, data is sent
        std::vector<BuilderMsg*> read() {
            std::vector<BuilderMsg*> sent;
            for (BuilderMsg* msg = cursor.next(*builder); msg != nullptr; msg = cursor.next(*builder)) {
                writer->hidden(msg->id);
                if (msg->isFlagSet(BuilderMsg::OUTPUT_BUFFER::CHECKPOINT))
                    writer->local(msg);
                else {
                    writer->sent(msg);
                    sent.push_back(msg);
                }
            }
            writer->idle();
            return sent;
        }
    };

    // Debezium 3.7.0 holds the last transaction while no captured table changes
    void heldDuringIdle() {
        Side side(false);
        side.checkpoint(100);
        side.read();
        check("hidden checkpoint with an empty queue is confirmed at once", side.writer->confirmed() == Scn(100));
        check("no message from the buffers confirmed yet", side.writer->messageConfirmed() == Scn::none());

        side.transaction(1, 150);
        const std::vector<BuilderMsg*> held = side.read();
        const uint64_t buffers = side.builder->buffersAllocated;
        const uint64_t firstBuffer = side.builder->firstBuilderQueue->id;
        bool flat = true;
        for (uint64_t scn = 200; scn < 1000200; ++scn) {
            side.checkpoint(scn);
            // The writer polls far less often than LWNs come
            if (scn % 100 == 0) {
                side.read();
                if (side.writer->held() > 1 || side.writer->queued() != 1 || BuilderTestAccess::hiddenSize(*side.builder) > 0)
                    flat = false;
            }
        }
        side.read();
        check("1M idle LWNs behind a held transaction: one held position, empty hidden list", flat && side.writer->held() == 1 &&
              BuilderTestAccess::hiddenSize(*side.builder) == 0);
        check("1M idle LWNs behind a held transaction: no builder buffer allocated", side.builder->buffersAllocated == buffers &&
              side.builder->firstBuilderQueue->id == firstBuffer);
        check("confirmed position stays before the held transaction", side.writer->confirmed() == Scn(100));

        side.writer->confirmMessage(held.at(0));
        check("held transaction confirmed: position at the last hidden checkpoint", side.writer->queued() == 0 &&
              side.writer->confirmed() == Scn(1000199) && side.writer->confirmedIndex() == 1);
        check("newest message confirmed is the transaction", side.writer->messageConfirmed() == Scn(150));
    }

    // A hidden checkpoint written after a message the writer has not read is not taken before it
    void order() {
        Side side(false);
        side.transaction(1, 100);
        side.checkpoint(110);
        BuilderMsg* msg = side.cursor.next(*side.builder);
        check("hidden checkpoint after an unread message is not taken", !side.writer->hidden(msg->id) && side.writer->confirmed() == Scn::none());
        side.writer->sent(msg);
        check("taken after the message is read, held while it is unconfirmed", side.writer->idle() && side.writer->held() == 1 &&
              side.writer->confirmed() == Scn::none());
        side.writer->confirmMessage(msg);
        check("taken once the message is confirmed", side.writer->confirmed() == Scn(110) && side.writer->confirmedIndex() == 1);
    }

    void redoFlag() {
        Side side(false);
        side.checkpoint(100, true);
        side.checkpoint(101, false);
        side.writer->clearRedo();
        side.read();
        check("a log switch checkpoint merged with a later one still forces the checkpoint write", side.writer->redoSet());
    }

    // Positions confirmed are the ones of the written checkpoints (fix/queue-checkpoint-stall), whatever the client does
    void sameAsWritten() {
        Side written(true);
        Side hidden(false);
        bool same = true;
        bool sameMessages = true;
        std::vector<BuilderMsg*> unconfirmedW;
        std::vector<BuilderMsg*> unconfirmedH;
        uint32_t xid = 0;
        uint64_t scn = 1000;
        auto step = [&] {
            const std::vector<BuilderMsg*> w = written.read();
            const std::vector<BuilderMsg*> h = hidden.read();
            if (w.size() != h.size())
                sameMessages = false;
            for (uint64_t i = 0; i < w.size() && i < h.size(); ++i)
                if (w[i]->lwnScn != h[i]->lwnScn || w[i]->lwnIdx != h[i]->lwnIdx || w[i]->id != h[i]->id)
                    sameMessages = false;
            unconfirmedW.insert(unconfirmedW.end(), w.begin(), w.end());
            unconfirmedH.insert(unconfirmedH.end(), h.begin(), h.end());
            if (written.writer->confirmed() != hidden.writer->confirmed() || written.writer->confirmedIndex() != hidden.writer->confirmedIndex())
                same = false;
        };
        auto both = [&](auto action) {
            action(written);
            action(hidden);
            step();
        };
        // Confirm the oldest n unconfirmed messages, in order or the newer one first
        auto confirm = [&](uint64_t n, bool reverse) {
            n = std::min<uint64_t>(n, unconfirmedW.size());
            for (uint64_t k = 0; k < n; ++k) {
                const uint64_t i = reverse ? n - 1 - k : k;
                written.writer->confirmMessage(unconfirmedW[i]);
                hidden.writer->confirmMessage(unconfirmedH[i]);
                step();
            }
            unconfirmedW.erase(unconfirmedW.begin(), unconfirmedW.begin() + static_cast<int64_t>(n));
            unconfirmedH.erase(unconfirmedH.begin(), unconfirmedH.begin() + static_cast<int64_t>(n));
        };

        for (uint64_t round = 0; round < 3000; ++round) {
            const uint64_t r = (round * 2654435761U) % 13;
            if (r < 5) {
                for (uint64_t i = 0; i <= r; ++i)
                    both([&](Side& s) { s.checkpoint(scn + i, (scn + i) % 11 == 0); });
                scn += r + 1;
            } else if (r < 9) {
                ++xid;
                both([&](Side& s) { s.transaction(xid, scn + 1); });
                // Same LWN: a checkpoint below the commit SCN takes no index of its own
                both([&](Side& s) { s.checkpoint(scn); });
                scn += 2;
            } else if (r < 11)
                confirm(r - 8, false);
            else if (r == 11)
                confirm(2, true);
            else
                confirm(unconfirmedW.size(), false);
        }
        confirm(unconfirmedW.size(), false);
        check("same messages sent as with written checkpoints", sameMessages);
        check("same confirmed position after every step as with written checkpoints", same);
        check("both end at the last checkpoint", hidden.writer->confirmed() == written.writer->confirmed() &&
              hidden.writer->queued() == 0 && hidden.writer->held() == 0);
    }

    // A reconnect drops what the queue held; checkpoints not taken yet stay in the builder and are taken in order again
    void reset() {
        Side side(false);
        side.transaction(1, 100);
        side.checkpoint(105);
        BuilderMsg* first = side.read().at(0);
        check("held behind the sent transaction", side.writer->held() == 1);
        side.transaction(2, 110);
        side.checkpoint(115);
        side.writer->reset();
        check("reconnect drops the queue and the held positions", side.writer->queued() == 0 && side.writer->held() == 0);
        check("checkpoint not taken yet stays in the builder", BuilderTestAccess::hiddenSize(*side.builder) == 1);
        // The client resends from its position: both transactions again
        side.cursor = Cursor{};
        const std::vector<BuilderMsg*> again = side.read();
        check("both transactions sent again", again.size() == 2 && again.at(0) == first);
        check("no position before the resent transactions are confirmed", side.writer->confirmed() == Scn::none());
        side.writer->confirmMessage(again.at(0));
        check("first confirmed: the checkpoint before the reconnect is not taken again", side.writer->confirmed() == Scn(100));
        side.writer->confirmMessage(again.at(1));
        check("second confirmed: the checkpoint after it", side.writer->confirmed() == Scn(115));
    }

    // CONTINUE gap: only messages from the buffers are released, a hidden checkpoint never is
    void continueGap() {
        Side side(false);
        side.checkpoint(100);
        side.read();
        side.checkpoint(200);
        side.read();
        Scn oldestScn = Scn::none();
        typeIdx oldestIdx = 0;
        const Writer::HELD held = side.writer->oldestHeldMessage(oldestScn, oldestIdx);
        check("no data since the start: nothing held", held == Writer::HELD::NONE);
        check("no data since the start: a client before the confirmed position misses nothing",
              !Writer::isContinueGap(Scn(150), 0, side.writer->messageConfirmed(), 0, held, oldestScn, oldestIdx));
        check("no data since the start: the position of the hidden checkpoints would report a gap",
              Writer::isContinueGap(Scn(150), 0, side.writer->confirmed(), side.writer->confirmedIndex(), held, oldestScn, oldestIdx));
    }

    // The real writer loop against a parser writing at the same time
    void threaded(bool holdLast) {
        Side side(false);
        side.writer->holdLast = holdLast;
        side.metadata->status = Metadata::STATUS::REPLICATING;
        std::thread writerThread([&side] { side.writer->loop(); });

        uint32_t xid = 0;
        uint64_t scn = 1000;
        std::vector<std::pair<Scn, typeIdx>> sent;
        for (uint64_t round = 0; round < 200000; ++round) {
            if (round % 5 == 0) {
                side.transaction(++xid, scn + 1);
                sent.emplace_back(side.builder->lwnScn, side.builder->lwnIdx);
                side.checkpoint(scn);
                scn += 2;
            } else
                side.checkpoint(scn++, round % 1000 == 0);
            if (round % 10000 == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        side.checkpoint(scn);
        const Scn lastScn = side.builder->lwnScn;
        const typeIdx lastIdx = side.builder->lwnIdx;
        side.ctx.softShutdown = true;
        side.ctx.replicatorFinished = true;
        side.builder->wakeUp();
        writerThread.join();

        const std::string name = holdLast ? "threaded, client holds the last transaction" : "threaded";
        check(name + ": every transaction sent once, in order", side.writer->received == sent);
        check(name + ": confirmed position never ahead of a message not sent yet", !side.writer->ahead);
        if (holdLast) {
            check(name + ": position stays before the held transaction", side.writer->confirmed() < sent.back().first);
            side.writer->releaseLast();
        }
        check(name + ": confirmed position ends at the last checkpoint", side.writer->confirmed() == lastScn &&
              side.writer->confirmedIndex() == lastIdx);
        check(name + ": a checkpoint is written at the end", !side.writer->checkpoints.empty());
    }
}

int main() {
    heldDuringIdle();
    order();
    redoFlag();
    sameAsWritten();
    reset();
    continueGap();
    threaded(false);
    threaded(true);

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
