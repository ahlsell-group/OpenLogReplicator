/* Test of the writer queue: metrics, the REDO flag of held positions, the queue full warning
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
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../builder/BuilderJson.h"
#include "../common/Clock.h"
#include "../common/Ctx.h"
#include "../common/Format.h"
#include "../common/Thread.h"
#include "../common/metrics/Metrics.h"
#include "../common/types/FileOffset.h"
#include "../locales/Locales.h"
#include "../metadata/Metadata.h"
#include "../writer/Writer.h"

// The writer queue as a client like Debezium sees it: the gauges follow the queue, a log switch checkpoint forces the
// checkpoint write when its position is applied (not when it is read), and a full queue is reported as one commit scn
// (60040) only when every queued message has that scn.
namespace OpenLogReplicator {
    class BuilderTestAccess {
    public:
        static void transaction(Builder& builder, uint32_t xid, Scn commitScn, const AttributeMap* attributes) {
            builder.processBegin(Xid(1, 1, xid), 1, Seq(1), commitScn, Time(0), Seq(1), commitScn, Time(0), attributes);
            builder.processBeginMessage(Seq(1), Time(0));
            builder.processCommit();
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

    class TestClock final : public Clock {
    public:
        time_ut now{1000000000};
        [[nodiscard]] time_ut getTimeUt() const override { return now; }
        [[nodiscard]] time_t getTimeT() const override { return static_cast<time_t>(now / 1000000); }
    };

    class TestMetrics final : public Metrics {
    public:
        int64_t queueSize{-1};
        int64_t heldPositions{-1};
        int64_t oldestSentUs{-1};

        TestMetrics(): Metrics(TAG_NAMES::NONE) {}
        void initialize(const Ctx*) override {}
        void shutdown() override {}
        void emitWriterQueueSize(int64_t gauge) override { queueSize = gauge; }
        void emitWriterQueueHeldPositions(int64_t gauge) override { heldPositions = gauge; }
        void emitWriterQueueOldestSentUs(int64_t timeUs) override { oldestSentUs = timeUs; }
        void emitBytesConfirmed(uint64_t) override {}
        void emitBytesParsed(uint64_t) override {}
        void emitBytesRead(uint64_t) override {}
        void emitBytesSent(uint64_t) override {}
        void emitCheckpointsOut(uint64_t) override {}
        void emitCheckpointsSkip(uint64_t) override {}
        void emitCheckpointLag(int64_t) override {}
        void emitDdlOpsAlter(uint64_t) override {}
        void emitDdlOpsCreate(uint64_t) override {}
        void emitDdlOpsDrop(uint64_t) override {}
        void emitDdlOpsOther(uint64_t) override {}
        void emitDdlOpsPurge(uint64_t) override {}
        void emitDdlOpsTruncate(uint64_t) override {}
        void emitDmlOpsDeleteOut(uint64_t) override {}
        void emitDmlOpsInsertOut(uint64_t) override {}
        void emitDmlOpsUpdateOut(uint64_t) override {}
        void emitDmlOpsDeleteSkip(uint64_t) override {}
        void emitDmlOpsInsertSkip(uint64_t) override {}
        void emitDmlOpsUpdateSkip(uint64_t) override {}
        void emitDmlOpsDeleteOut(uint64_t, const std::string&, const std::string&) override {}
        void emitDmlOpsInsertOut(uint64_t, const std::string&, const std::string&) override {}
        void emitDmlOpsUpdateOut(uint64_t, const std::string&, const std::string&) override {}
        void emitDmlOpsDeleteSkip(uint64_t, const std::string&, const std::string&) override {}
        void emitDmlOpsInsertSkip(uint64_t, const std::string&, const std::string&) override {}
        void emitDmlOpsUpdateSkip(uint64_t, const std::string&, const std::string&) override {}
        void emitLogSwitchesArchived(uint64_t) override {}
        void emitLogSwitchesOnline(uint64_t) override {}
        void emitLogSwitchesLagArchived(int64_t) override {}
        void emitLogSwitchesLagOnline(int64_t) override {}
        void emitMemoryAllocatedMb(int64_t) override {}
        void emitMemoryUsedTotalMb(int64_t) override {}
        void emitMemoryUsedMbBuilder(int64_t) override {}
        void emitMemoryUsedMbMisc(int64_t) override {}
        void emitMemoryUsedMbParser(int64_t) override {}
        void emitMemoryUsedMbReader(int64_t) override {}
        void emitMemoryUsedMbTransactions(int64_t) override {}
        void emitMemoryUsedMbWriter(int64_t) override {}
        void emitMessagesConfirmed(uint64_t) override {}
        void emitMessagesSent(uint64_t) override {}
        void emitServiceStateInitializing(int64_t) override {}
        void emitServiceStateReady(int64_t) override {}
        void emitServiceStateStarting(int64_t) override {}
        void emitServiceStateReplicating(int64_t) override {}
        void emitServiceStateFinishing(int64_t) override {}
        void emitServiceStateAborting(int64_t) override {}
        void emitSwapOperationsMbDiscard(uint64_t) override {}
        void emitSwapOperationsMbRead(uint64_t) override {}
        void emitSwapOperationsMbWrite(uint64_t) override {}
        void emitSwapUsageMb(int64_t) override {}
        void emitTransactionsCommitOut(uint64_t) override {}
        void emitTransactionsRollbackOut(uint64_t) override {}
        void emitTransactionsCommitPartial(uint64_t) override {}
        void emitTransactionsRollbackPartial(uint64_t) override {}
        void emitTransactionsCommitSkip(uint64_t) override {}
        void emitTransactionsRollbackSkip(uint64_t) override {}
    };

    class TestWriter final : public Writer {
    public:
        TestWriter(Ctx* newCtx, Builder* newBuilder, Metadata* newMetadata): Writer(newCtx, "test-writer", "TEST", newBuilder, newMetadata) {}

        // Calls as mainLoop makes them
        void sent(BuilderMsg* msg) { createMessage(msg); }
        void local(BuilderMsg* msg) { confirmLocalMessage(msg); }
        bool hidden(uint64_t beforeId) { return confirmHiddenCheckpoints(beforeId); }
        bool idle() { return confirmHiddenCheckpoints(builder->hiddenEndId); }
        void tick() { writeCheckpoint(redo); }
        void reset() { resetMessageQueue(); }
        void warn(time_ut fullSince, time_ut& fullWarned) { warnQueueFull(fullSince, fullWarned); }
        uint64_t queued() const { return currentQueueSize; }
        uint64_t held() const { return localHeld.size(); }
        uint64_t sentTimes() const { return queueSent.size(); }
        Scn confirmed() const { return confirmedScn; }
        bool redoSet() const { return redo; }

        // Checkpoint writes the loop forces
        std::vector<std::pair<Scn, typeIdx>> forced;

    protected:
        void sendMessage(BuilderMsg*) override {}
        std::string getType() const override { return "test"; }
        void pollQueue() override {}
        void writeCheckpoint(bool force) override {
            redo = false;
            if ((checkpointScn == confirmedScn && checkpointIdx == confirmedIdx) || confirmedScn == Scn::none())
                return;
            if (force)
                forced.emplace_back(confirmedScn, confirmedIdx);
            checkpointScn = confirmedScn;
            checkpointIdx = confirmedIdx;
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
        TestClock* clock{nullptr};
        TestMetrics* metrics{nullptr};
        Locales locales;
        Format format{makeFormat()};
        std::unique_ptr<Metadata> metadata;
        std::unique_ptr<BuilderJson> builder;
        std::unique_ptr<TestWriter> writer;
        Cursor cursor;
        AttributeMap attributes;

        Side(bool show, bool withMetrics, uint64_t queueSize = 1000) {
            ctx.parserThread = &thread;
            ctx.initialize(32, 512, 16, 16, 0, 0, 256, 16);
            ctx.queueSize = queueSize;
            ctx.pollIntervalUs = 100;
            if (show)
                ctx.flags |= static_cast<uint>(Ctx::REDO_FLAGS::SHOW_CHECKPOINT);
            delete ctx.clock;
            clock = new TestClock();
            ctx.clock = clock;
            if (withMetrics) {
                metrics = new TestMetrics();
                ctx.metrics = metrics;
            }
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

        // The writer reads everything written: checkpoint messages are confirmed locally, data is sent
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

    // Captures the log of one call
    class CaptureCerr {
        std::ostringstream out;
        std::streambuf* old;

    public:
        CaptureCerr(): old(std::cerr.rdbuf(out.rdbuf())) {}
        ~CaptureCerr() { std::cerr.rdbuf(old); }
        std::string text() const { return out.str(); }
    };

    void metrics() {
        Side side(false, true);
        TestMetrics& m = *side.metrics;
        side.checkpoint(50);
        side.read();
        check("metrics: empty queue", m.queueSize == 0 && m.heldPositions == 0 && m.oldestSentUs == 0);

        side.clock->now = 2000000000;
        side.transaction(1, 100);
        side.checkpoint(105);
        const std::vector<BuilderMsg*> first = side.read();
        check("metrics: one sent, hidden checkpoint held behind it", m.queueSize == 1 && m.heldPositions == 1 &&
              m.oldestSentUs == 2000000000);

        side.clock->now = 2005000000;
        side.transaction(2, 110);
        side.checkpoint(115);
        const std::vector<BuilderMsg*> second = side.read();
        check("metrics: two sent, the oldest keeps its send time", m.queueSize == 2 && m.heldPositions == 2 &&
              m.oldestSentUs == 2000000000);

        // A writer like Kafka confirms out of order: nothing leaves the queue before the oldest message
        side.writer->confirmMessage(second.at(0));
        check("metrics: newer message confirmed first, the queue still waits for the oldest", m.queueSize == 2 && m.heldPositions == 2 &&
              m.oldestSentUs == 2000000000);
        side.writer->confirmMessage(first.at(0));
        check("metrics: both confirmed, queue empty", m.queueSize == 0 && m.heldPositions == 0 && m.oldestSentUs == 0 &&
              side.writer->sentTimes() == 0 && side.writer->confirmed() == Scn(115));

        side.clock->now = 3000000000;
        side.transaction(3, 120);
        const std::vector<BuilderMsg*> third = side.read();
        side.clock->now = 3001000000;
        side.transaction(4, 130);
        side.read();
        side.writer->confirmMessage(third.at(0));
        check("metrics: the oldest confirmed, the next one is the oldest", m.queueSize == 1 && m.oldestSentUs == 3001000000 &&
              side.writer->sentTimes() == 1);

        side.writer->reset();
        check("metrics: a reconnect empties the queue", m.queueSize == 0 && m.heldPositions == 0 && m.oldestSentUs == 0 &&
              side.writer->sentTimes() == 0);

        Side none(false, false);
        none.transaction(1, 100);
        none.read();
        check("no metrics: no send times kept", none.writer->queued() == 1 && none.writer->sentTimes() == 0);
    }

    // A log switch checkpoint held behind a sent message forces the write when it is applied
    void redoHeldHidden() {
        Side side(false, false);
        side.checkpoint(50);
        side.read();
        side.writer->tick();
        side.writer->forced.clear();

        side.transaction(1, 100);
        side.checkpoint(105, true);
        const std::vector<BuilderMsg*> sent = side.read();
        check("redo hidden: held behind the sent transaction", side.writer->held() == 1 && side.writer->confirmed() == Scn(50));
        check("redo hidden: not set before the position is applied", !side.writer->redoSet());
        side.writer->tick();
        check("redo hidden: no forced write of the older position", side.writer->forced.empty());

        side.writer->confirmMessage(sent.at(0));
        check("redo hidden: set when the position is applied", side.writer->redoSet() && side.writer->confirmed() == Scn(105));
        side.writer->tick();
        check("redo hidden: the forced write has the log switch position", side.writer->forced.size() == 1 &&
              side.writer->forced.at(0).first == Scn(105));
    }

    // A held log switch checkpoint dropped on reconnect still forces the next write that moves the position
    void redoHeldReset() {
        Side side(false, false);
        side.checkpoint(50);
        side.read();
        side.writer->tick();
        side.writer->forced.clear();

        side.transaction(1, 100);
        side.checkpoint(105, true);
        side.read();
        check("redo reset: held behind the sent transaction", side.writer->held() == 1);
        side.writer->reset();
        check("redo reset: held position dropped", side.writer->held() == 0 && !side.writer->redoSet());

        side.transaction(2, 110);
        const std::vector<BuilderMsg*> sent = side.read();
        side.writer->confirmMessage(sent.at(0));
        check("redo reset: set when the position moves", side.writer->redoSet() && side.writer->confirmed() == Scn(110));
        side.writer->tick();
        check("redo reset: forced write", side.writer->forced.size() == 1 && side.writer->forced.at(0).first == Scn(110));
    }

    // Merged with a later checkpoint in the same run, the flag stays
    void redoHeldMerged() {
        Side side(false, false);
        side.transaction(1, 100);
        side.checkpoint(105, true);
        side.checkpoint(106);
        const std::vector<BuilderMsg*> sent = side.read();
        side.writer->tick();
        side.writer->confirmMessage(sent.at(0));
        side.writer->tick();
        check("redo merged: forced write at the merged position", side.writer->forced.size() == 1 &&
              side.writer->forced.at(0).first == Scn(106));
    }

    // With SHOW_CHECKPOINT the log switch checkpoint is sent and queued
    void redoQueued() {
        Side side(true, false);
        side.transaction(1, 100);
        side.checkpoint(105, true);
        BuilderMsg* data = side.cursor.next(*side.builder);
        BuilderMsg* chkpt = side.cursor.next(*side.builder);
        side.writer->sent(data);
        side.writer->sent(chkpt);
        check("redo queued: not set when the message is read", !side.writer->redoSet());
        side.writer->tick();
        side.writer->confirmMessage(chkpt);
        check("redo queued: not set while an older message is unconfirmed", !side.writer->redoSet() && side.writer->queued() == 2);
        side.writer->confirmMessage(data);
        check("redo queued: set when it leaves the queue", side.writer->redoSet());
        side.writer->tick();
        check("redo queued: forced write at the log switch position", side.writer->forced.size() == 1 &&
              side.writer->forced.at(0).first == Scn(105));
    }

    // A checkpoint message the writer confirms itself: at once with an empty queue, else when its run is applied
    void redoLocal() {
        Side side(true, false);
        side.checkpoint(50, true);
        side.writer->local(side.cursor.next(*side.builder));
        check("redo local: empty queue, set at once", side.writer->redoSet());
        side.writer->tick();

        side.transaction(1, 100);
        side.checkpoint(105, true);
        BuilderMsg* data = side.cursor.next(*side.builder);
        side.writer->sent(data);
        side.writer->local(side.cursor.next(*side.builder));
        check("redo local: held, not set", !side.writer->redoSet() && side.writer->held() == 1);
        side.writer->confirmMessage(data);
        check("redo local: set when applied", side.writer->redoSet());
        side.writer->tick();
        check("redo local: forced writes at both log switch positions", side.writer->forced.size() == 2 &&
              side.writer->forced.at(1).first == Scn(105));
    }

    std::string warnFull(Side& side) {
        time_ut warned = 0;
        CaptureCerr capture;
        side.writer->warn(side.clock->now - 11000000, warned);
        return capture.text();
    }

    void queueFull() {
        // Every queued message has one commit scn, the next one a later scn: a client which confirms only when the scn
        // grows can't confirm any of them
        {
            Side side(false, false, 3);
            for (uint32_t xid = 1; xid <= 3; ++xid)
                side.transaction(xid, 100);
            side.read();
            side.transaction(4, 101);
            const std::string log = warnFull(side);
            check("queue full of one commit scn: 60040 although the next message has a later scn", log.find("60040") != std::string::npos &&
                  log.find("scn: 100") != std::string::npos);
        }
        // A later scn is queued: the client could have confirmed the oldest one
        {
            Side side(false, false, 3);
            side.transaction(1, 100);
            side.transaction(2, 101);
            side.transaction(3, 101);
            side.read();
            side.transaction(4, 101);
            const std::string log = warnFull(side);
            check("queue full with a later scn queued: 60046 although the next message has the newest scn",
                  log.find("60046") != std::string::npos && log.find("scn: 100, idx: 1") != std::string::npos);
        }
        // Not full: no warning
        {
            Side side(false, false, 3);
            side.transaction(1, 100);
            side.read();
            check("queue not full: no warning", warnFull(side).empty());
        }
    }

    // A reconnect frees the copies of messages split over buffers
    void resetFreesCopies() {
        Side side(false, false);
        side.transaction(1, 100);
        BuilderMsg* msg = side.read().at(0);
        uint8_t* const inBuffer = msg->data;
        msg->data = new uint8_t[msg->size];
        msg->setFlag(BuilderMsg::OUTPUT_BUFFER::ALLOCATED);
        side.writer->reset();
        check("reset frees the copy of a split message", !msg->isFlagSet(BuilderMsg::OUTPUT_BUFFER::ALLOCATED));
        msg->data = inBuffer;
    }
}

int main() {
    metrics();
    redoHeldHidden();
    redoHeldReset();
    redoHeldMerged();
    redoQueued();
    redoLocal();
    queueFull();
    resetFreesCopies();

    if (failures > 0) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "all checks passed\n";
    return 0;
}
